/* XENSIV BGT60TRXX register configurator, SDK versionv3.4.0+.e7acc0f10 */
/*
    Generated using Infineon 3.4.x BGT-Configurator-CLI tool with the below config.json

    "device_config": {
        "fmcw_single_shape": {
            "rx_antennas": [1, 2, 3],
            "tx_antennas": [1],
            "tx_power_level": 31,
            "if_gain_dB": 18,
            "hp_cutoff_Hz": 80000,
            "start_frequency_Hz": 58000000000,
            "end_frequency_Hz": 59200000000,
            "num_chirps_per_frame": 32,
            "num_samples_per_chirp": 128,
            "chirp_repetition_time_s": 0.0001,
            "frame_repetition_time_s": 0.1,
            "sample_rate_Hz": 2000000
        }
    }
*/
#ifndef XENSIV_BGT60TRXX_CONF_H
#define XENSIV_BGT60TRXX_CONF_H

#define XENSIV_BGT60TRXX_CONF_DEVICE (XENSIV_DEVICE_BGT60TR13C)
#define XENSIV_BGT60TRXX_CONF_START_FREQ_HZ (58000000000)
#define XENSIV_BGT60TRXX_CONF_END_FREQ_HZ (59200000000)
#define XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP (128)
#define XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME (32)
#define XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS (3)
#define XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS (1)
#define XENSIV_BGT60TRXX_CONF_SAMPLE_RATE (2000000)
#define XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S (0.00010005)
#define XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S (0.10007)
#define XENSIV_BGT60TRXX_CONF_NUM_REGS (38)

#if defined(XENSIV_BGT60TRXX_CONF_IMPL)
const uint32_t register_list[] = { 
    0x11e8270UL, 
    0x30a0210UL, 
    0x9e967fdUL, 
    0xb0805b4UL, 
    0xdf02bffUL, 
    0xf010700UL, 
    0x11000000UL, 
    0x13000000UL, 
    0x15000000UL, 
    0x17000be0UL, 
    0x19000000UL, 
    0x1b000000UL, 
    0x1d000000UL, 
    0x1f000b60UL, 
    0x2113fc51UL, 
    0x237ff41fUL, 
    0x25700c63UL, 
    0x2d000490UL, 
    0x3b000480UL, 
    0x49000480UL, 
    0x57000480UL, 
    0x5911be0eUL, 
    0x5b67540aUL, 
    0x5d01f000UL, 
    0x5f787e1eUL, 
    0x61a9298cUL, 
    0x6300016eUL, 
    0x650002b2UL, 
    0x67000080UL, 
    0x69000000UL, 
    0x6b000000UL, 
    0x6d000000UL, 
    0x6f0c7110UL, 
    0x7f000100UL, 
    0x8f000100UL, 
    0x9f000100UL, 
    0xad000000UL, 
    0xb7000000UL
};
#endif /* XENSIV_BGT60TRXX_CONF_IMPL */

#endif /* XENSIV_BGT60TRXX_CONF_H */