# Vendored from Infineon sensor-xensiv-bgt60trxx

- Repo: https://github.com/Infineon/sensor-xensiv-bgt60trxx
- Commit: 78099697159a2a736504ef5bd1d33820b21f4e67
- Tag: release-v2.0.0
- Vendored on: 2026-09-15
- License: Apache-2.0 (see LICENSE / NOTICE in this directory)
- Local modifications: yes, to the FIFO read only. See below, and
  0001-fifo-read-as-one-spi-transfer.patch for the exact diff against the
  commit above.

## Local modifications

### The FIFO burst read is one SPI transfer

Files: `xensiv_bgt60trxx.c` (`xensiv_bgt60trxx_get_fifo_data()`) and
`xensiv_bgt60trxx_platform.h` (`xensiv_bgt60trxx_platform_spi_fifo_read()`).

Reading the FIFO is one SPI burst: chip select (CS) goes low, the host
sends the 4-byte burst command while the sensor returns its status,
including GSR0, in the same four bytes, the FIFO data follows, and CS goes
high. CS has to stay low from the command to the last data byte.

Upstream splits the burst into two platform calls and holds CS across them
itself:

1. `xensiv_bgt60trxx_platform_spi_cs_set(false)` asserts CS.
2. `xensiv_bgt60trxx_platform_spi_transfer()` sends the command, and the
   library checks the GSR0 status that comes back.
3. `xensiv_bgt60trxx_platform_spi_fifo_read()` reads the data.
4. `xensiv_bgt60trxx_platform_spi_cs_set(true)` releases CS.

That needs a platform that lets the library drive CS. Zephyr's doesn't: CS
belongs to the SPI controller driver, which asserts it at the start of each
`spi_transceive()` and releases it at the end. `SPI_HOLD_ON_CS` only asks
the controller to keep CS low after a call "if supported and possible", so
it can't be relied on. `xensiv_bgt60trxx_platform_spi_cs_set()` is
therefore a no-op here (`../bgt60tr13c_platform.c`), and with the upstream
code CS would go high between steps 2 and 3, ending the burst before any
data was read.

The change:

- `xensiv_bgt60trxx_platform_spi_fifo_read()` takes the burst command as a
  new `reg_addr` argument, ahead of `rx_data`, so the platform can send the
  command and read the data in one transfer, under one CS assertion.
- `xensiv_bgt60trxx_get_fifo_data()` builds the command as before and
  passes it on, instead of driving CS and sending the command itself.
- The GSR0 check moves to the platform, which is where the status bytes now
  arrive. `bgt60tr13c_fifo_read_status()` in `../bgt60tr13c_platform.c`
  checks the same bits (FOU_ERR, SPI_BURST_ERR, CLK_NUM_ERR) and returns
  the same `XENSIV_BGT60TRXX_STATUS_GSR0_ERROR`.
- The two `num_samples` assertions at the top of
  `xensiv_bgt60trxx_get_fifo_data()` (even, and no more than the FIFO
  holds) were removed in the same change.
- Comments marking the change in both files.

`xensiv_bgt60trxx_get_fifo_data()` keeps its signature, so the library's
API is unchanged. The driver itself doesn't call it: its own FIFO reads,
`bgt60tr13c_fifo_read_async()`, send the same single-transfer burst.

A platform hook that performs the whole burst, command included, would let
platforms like Zephyr use the library without this change.
