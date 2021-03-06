// iWRAP external host controller library generic demo
// 2014-05-25 by Jeff Rowberg <jeff@rowberg.net>
//
// Changelog:
//  2014-05-25 - Initial release

/* ============================================
iWRAP host controller library code is placed under the MIT license
Copyright (c) 2014 Jeff Rowberg

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
===============================================
*/

// iWRAP firmware should be v5.0.2 (build 861 or higher)
// Config settings may vary, but ideally you will at least enable a few
// special config bits and MUX mode with the following commands:
//
//      SET CONTROL CONFIG 3400 0040 70A1
//      SET CONTROL MUX 1
//
// See the iWRAP User Guide from Bluegiga for more detail. The following
// pre-built MUX frames can be handy for changing various settings, e.g.
// using an app like Realterm with the "Send" tab and "Send Numbers"
// button to send a hex string while the module is configured with MUX
// mode enabled:
//
// MUX frame for "SET CONTROL MUX 0" (disable MUX mode):
//      $bf $ff $00 $17 $53 $45 $54 $20 $43 $4f $4e $54 $52 $4f $4c $20 $4d $55 $58 $20 $30 $00
// MUX frame for "SET CONTROL BAUD 38400":
//      $bf $ff $00 $16 $53 $45 $54 $20 $43 $4f $4e $54 $52 $4f $4c $20 $42 $41 $55 $44 $20 $33 $38 $34 $30 $30 $00
// MUX frame for "SET CONTROL BAUD 115200":
//      $bf $ff $00 $17 $53 $45 $54 $20 $43 $4f $4e $54 $52 $4f $4c $20 $42 $41 $55 $44 $20 $31 $31 $35 $32 $30 $30 $00

#include <Arduino.h>
//#include <avr/pgmspace.h>

//#define PLATFORM_ARDUINO_UNO    // also Pro Mini and other ATMega328-based boards
#define PLATFORM_TEENSY2        // both Teensy 2.0 and Teensy++ 2.0

#ifdef PLATFORM_ARDUINO_UNO
    #include <AltSoftSerial.h>
    AltSoftSerial mySerial; // RX=8, TX=9
#endif

#define HOST_BAUD                   38400   // works with 8MHz CPU clock
#define IWRAP_BAUD                  38400   // works with 8MHz CPU clock (REQUIRES iWRAP RECONFIGURATION, DEFAULT IS 115200)
#define MODULE_RESET_PIN            12      // optional connection for MCU-based module reset

// -------- iWRAP configuration definitions --------
// COPY TO TOP OF "iWRAP.h" TO APPLY TO THIS PROGRAM
// OR ELSE ALL FEATURES ENABLED DUE TO BUILD SYSTEM
// -------------------------------------------------
//#define IWRAP_DEBUG
#define IWRAP_INCLUDE_RSP_CALL
#define IWRAP_INCLUDE_RSP_LIST_COUNT
#define IWRAP_INCLUDE_RSP_LIST_RESULT
#define IWRAP_INCLUDE_RSP_SET
#define IWRAP_INCLUDE_EVT_CONNECT
#define IWRAP_INCLUDE_EVT_NO_CARRIER
#define IWRAP_INCLUDE_EVT_PAIR
#define IWRAP_INCLUDE_EVT_READY
#define IWRAP_INCLUDE_EVT_RING
#define IWRAP_CONFIGURED
// -------------------------------------------------

#include <iWRAP.h>

#define IWRAP_STATE_IDLE            0
#define IWRAP_STATE_UNKNOWN         1
#define IWRAP_STATE_PENDING_AT      2
#define IWRAP_STATE_PENDING_SET     3
#define IWRAP_STATE_PENDING_LIST    4
#define IWRAP_STATE_PENDING_CALL    5
#define IWRAP_STATE_COMM_FAILED     255

#define IWRAP_MAX_PAIRINGS          16

// connection map structure
typedef struct {
    iwrap_address_t mac;
    uint8_t active_links;
    uint8_t link_a2dp1;
    uint8_t link_a2dp2;
    uint8_t link_avrcp;
    uint8_t link_hfp;
    uint8_t link_hfpag;
    uint8_t link_hid_control;
    uint8_t link_hid_interrupt;
    uint8_t link_hsp;
    uint8_t link_hspag;
    uint8_t link_iap;
    uint8_t link_sco;
    uint8_t link_spp;
    // other profile-specific link IDs may be added here
} iwrap_connection_t;
iwrap_connection_t *iwrap_connection_map[IWRAP_MAX_PAIRINGS];

// iwrap state tracking info
uint8_t iwrap_mode = IWRAP_MODE_MUX;
uint8_t iwrap_state = IWRAP_STATE_UNKNOWN;
uint8_t iwrap_initialized = 0;
uint32_t iwrap_time_ref = 0;
uint8_t iwrap_pairings = 0;
uint8_t iwrap_pending_calls = 0;
uint8_t iwrap_pending_call_link_id = 0xFF;
uint8_t iwrap_connected_devices = 0;
uint8_t iwrap_active_connections = 0;
uint8_t iwrap_autocall_target = 0;
uint16_t iwrap_autocall_delay_ms = 10000;
uint32_t iwrap_autocall_last_time = 0;
uint8_t iwrap_autocall_index = 0;

// iWRAP callbacks necessary for application
void my_iwrap_rsp_call(uint8_t link_id);
void my_iwrap_rsp_list_count(uint8_t num_of_connections);
void my_iwrap_rsp_list_result(uint8_t link_id, const char *mode, uint16_t blocksize, uint32_t elapsed_time, uint16_t local_msc, uint16_t remote_msc, const iwrap_address_t *addr, uint16_t channel, uint8_t direction, uint8_t powermode, uint8_t role, uint8_t crypt, uint16_t buffer, uint8_t eretx);
void my_iwrap_rsp_set(uint8_t category, const char *option, const char *value);
void my_iwrap_evt_connect(uint8_t link_id, const char *type, uint16_t target, const iwrap_address_t *address);
void my_iwrap_evt_no_carrier(uint8_t link_id, uint16_t error_code, const char *message);
void my_iwrap_evt_pair(const iwrap_address_t *address, uint8_t key_type, const uint8_t *link_key);
void my_iwrap_evt_ready();
void my_iwrap_evt_ring(uint8_t link_id, const iwrap_address_t *address, uint8_t channel, const char *profile);

// general helper functions
uint8_t find_pairing_from_mac(const iwrap_address_t *mac);
uint8_t find_pairing_from_link_id(uint8_t link_id);
void add_mapped_connection(uint8_t link_id, const iwrap_address_t *addr, const char *mode, uint16_t channel);
uint8_t remove_mapped_connection(uint8_t link_id);
void print_connection_map();
void print_demo_menu();
void process_demo_choice(char b);

// platform-specific helper functions
int serial_out(const char *str);
int serial_out(const __FlashStringHelper *str);
int iwrap_out(int len, unsigned char *data);

void setup() {
    #if defined(PLATFORM_ARDUINO_UNO)
        // open the hardware serial port for debug/status output and software serial for iWRAP
        Serial.begin(HOST_BAUD);
        mySerial.begin(IWRAP_BAUD);
    #elif defined(PLATFORM_TEENSY2)
        // open the USB serial port for debug/status output and Serial1 for iWRAP
        Serial.begin(HOST_BAUD);
        Serial1.begin(IWRAP_BAUD);
    #else
        #error Select a supported platform, or add support for your own
    #endif
    
    // setup optional hardware reset pin connection (digital pin 9)
    digitalWrite(MODULE_RESET_PIN, LOW);
    pinMode(MODULE_RESET_PIN, OUTPUT);

    // assign transport/debug output
    iwrap_output = iwrap_out;
    #ifdef IWRAP_DEBUG
        iwrap_debug = serial_out;
    #endif /* IWRAP_DEBUG */
    
    // assign event callbacks
    iwrap_rsp_call = my_iwrap_rsp_call;
    iwrap_rsp_list_count = my_iwrap_rsp_list_count;
    iwrap_rsp_list_result = my_iwrap_rsp_list_result;
    iwrap_rsp_set = my_iwrap_rsp_set;
    iwrap_evt_connect = my_iwrap_evt_connect;
    iwrap_evt_no_carrier = my_iwrap_evt_no_carrier;
    iwrap_evt_pair = my_iwrap_evt_pair;
    iwrap_evt_ready = my_iwrap_evt_ready;
    iwrap_evt_ring = my_iwrap_evt_ring;
    
    // boot message to host
    serial_out(F("iWRAP host library generic demo started\n"));
}

void loop() {
    uint16_t result;

    // manage iWRAP state machine
    if (!iwrap_pending_commands) {
        // no pending commands, some state transition occurring
        if (iwrap_state) {
            // not idle, in the middle of some process
            if (iwrap_state == IWRAP_STATE_UNKNOWN) {
                // reset all detailed state trackers
                iwrap_initialized = 0;
                iwrap_pairings = 0;
                iwrap_pending_calls = 0;
                iwrap_pending_call_link_id = 0xFF;
                iwrap_connected_devices = 0;
                iwrap_active_connections = 0;
                
                // send command to test module connectivity
                serial_out(F("Testing iWRAP communication...\n"));
                iwrap_send_command("AT", iwrap_mode);
                iwrap_state = IWRAP_STATE_PENDING_AT;

                // initialize time reference for connectivity test timeout
                iwrap_time_ref = millis();
            } else if (iwrap_state == IWRAP_STATE_PENDING_AT) {
                // send command to dump all module settings and pairings
                serial_out(F("Getting iWRAP settings...\n"));
                iwrap_send_command("SET", iwrap_mode);
                iwrap_state = IWRAP_STATE_PENDING_SET;
            } else if (iwrap_state == IWRAP_STATE_PENDING_SET) {
                // send command to show all current connections
                serial_out(F("Getting active connection list...\n"));
                iwrap_send_command("LIST", iwrap_mode);
                iwrap_state = IWRAP_STATE_PENDING_LIST;
            } else if (iwrap_state == IWRAP_STATE_PENDING_LIST) {
                // all done!
                if (!iwrap_initialized) {
                    iwrap_initialized = 1;
                    serial_out(F("iWRAP initialization complete\n"));
                }
                print_connection_map();
                print_demo_menu();
                iwrap_state = IWRAP_STATE_IDLE;
            } else if (iwrap_state == IWRAP_STATE_PENDING_CALL && !iwrap_pending_calls) {
                // all done!
                serial_out(F("Pending call processed\n"));
                iwrap_state = IWRAP_STATE_IDLE;
            }
        } else if (iwrap_initialized) {
            // idle
            if (iwrap_pairings && iwrap_autocall_target > iwrap_connected_devices && !iwrap_pending_calls
                               && (!iwrap_autocall_last_time || (millis() - iwrap_autocall_last_time) >= iwrap_autocall_delay_ms)) {
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 19 A2DP";      // A2DP
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 17 AVRCP";     // AVRCP
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 111F HFP";     // HFP
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 111E HFP-AG";  // HFP-AG
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 11 HID";       // HID
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 1112 HSP";     // HSP
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF 1108 HSP-AG";  // HSP-AG
                //char cmd[] = "CALL AA:BB:CC:DD:EE:FF * IAP";        // IAP
                char cmd[] = "CALL AA:BB:CC:DD:EE:FF 1101 RFCOMM";  // SPP
                char *cptr = cmd + 5;
                
                // find first unconnected device
                for (; iwrap_connection_map[iwrap_autocall_index] -> active_links; iwrap_autocall_index++);
                
                // write MAC string into call command buffer and send it
                iwrap_bintohexstr((uint8_t *)(iwrap_connection_map[iwrap_autocall_index] -> mac.address), 6, &cptr, ':', 0);
                char s[21];
                sprintf(s, "Calling device #%d\r\n", iwrap_autocall_index);
                serial_out(s);
                iwrap_send_command(cmd, iwrap_mode);
                iwrap_autocall_last_time = millis();
            }
        }
    }
    
    // check for incoming iWRAP data
    #if defined(PLATFORM_ARDUINO_UNO)
        if ((result = mySerial.read()) < 256) iwrap_parse(result & 0xFF, iwrap_mode);
    #elif defined(PLATFORM_TEENSY2)
        if ((result = Serial1.read()) < 256) iwrap_parse(result & 0xFF, iwrap_mode);
    #endif
    
    // check for timeout if still testing communication
    if (!iwrap_initialized && iwrap_state == IWRAP_STATE_PENDING_AT) {
        if (millis() - iwrap_time_ref > 5000) {
            serial_out(F("ERROR: Could not communicate with iWRAP module\n"));
            iwrap_state = IWRAP_STATE_COMM_FAILED;
            iwrap_pending_commands = 0; // normally handled by the parser, but comms failed
        }
    }

    // check for incoming host data
    #if defined(PLATFORM_ARDUINO_UNO)
        if ((result = Serial.read()) < 256)
    #elif defined(PLATFORM_TEENSY2)
        if ((result = Serial.read()) < 256)
    #endif
    {
        process_demo_choice(result & 0xFF);
    }
}

/* ============================================================================
 * IWRAP RESPONSE AND EVENT HANDLER IMPLEMENTATIONS
 * ========================================================================= */

void my_iwrap_rsp_call(uint8_t link_id) {
    iwrap_pending_calls++;
    iwrap_pending_call_link_id = link_id;
    iwrap_autocall_index = (iwrap_autocall_index + 1) % iwrap_pairings;
    iwrap_state = IWRAP_STATE_PENDING_CALL;
}

void my_iwrap_rsp_list_count(uint8_t num_of_connections) {
    iwrap_active_connections = num_of_connections;
}

void my_iwrap_rsp_list_result(uint8_t link_id, const char *mode, uint16_t blocksize, uint32_t elapsed_time, uint16_t local_msc, uint16_t remote_msc, const iwrap_address_t *addr, uint16_t channel, uint8_t direction, uint8_t powermode, uint8_t role, uint8_t crypt, uint16_t buffer, uint8_t eretx) {
    add_mapped_connection(link_id, addr, mode, channel);
}

void my_iwrap_rsp_set(uint8_t category, const char *option, const char *value) {
    if (category == IWRAP_SET_CATEGORY_BT) {
        if (strncmp((char *)option, "BDADDR", 6) == 0) {
            iwrap_address_t local_mac;
            iwrap_hexstrtobin((char *)value, 0, local_mac.address, 0);
            char *local_mac_str = (char *)malloc(18);
            if (local_mac_str) {
                iwrap_bintohexstr(local_mac.address, 6, &local_mac_str, ':', 1);
                serial_out(F(":: Module MAC is "));
                serial_out(local_mac_str);
                serial_out(F("\n"));
                free(local_mac_str);
            }
        } else if (strncmp((char *)option, "NAME", 4) == 0) {
            serial_out(F(":: Friendly name is "));
            serial_out(value);
            serial_out(F("\n"));
        } else if (strncmp((char *)option, "PAIR", 4) == 0) {
            iwrap_address_t remote_mac;
            iwrap_hexstrtobin((char *)value, 0, remote_mac.address, 0);
            
            // make sure we allocate memory for the connection map entry
            if (iwrap_connection_map[iwrap_pairings] == 0) {
                iwrap_connection_map[iwrap_pairings] = (iwrap_connection_t *)malloc(sizeof(iwrap_connection_t));
            }
            memset(iwrap_connection_map[iwrap_pairings], 0xFF, sizeof(iwrap_connection_t)); // 0xFF is "no link ID"
            memcpy(&(iwrap_connection_map[iwrap_pairings] -> mac), &remote_mac, sizeof(iwrap_address_t));
            iwrap_connection_map[iwrap_pairings] -> active_links = 0;
            iwrap_pairings++;
            
            char *remote_mac_str = (char *)malloc(18);
            if (remote_mac_str) {
                iwrap_bintohexstr(remote_mac.address, 6, &remote_mac_str, ':', 1);
                serial_out(F(":: Pairing (MAC="));
                serial_out(remote_mac_str);
                serial_out(F(", key="));
                serial_out(value + 18);
                serial_out(F(")\n"));
                free(remote_mac_str);
            }
        }
    }
}

void my_iwrap_evt_connect(uint8_t link_id, const char *type, uint16_t target, const iwrap_address_t *address) {
    if (iwrap_pending_call_link_id == link_id) {
        if (iwrap_pending_calls) iwrap_pending_calls--;
        if (iwrap_state == IWRAP_STATE_PENDING_CALL) iwrap_state = IWRAP_STATE_IDLE;
        iwrap_pending_call_link_id = 0xFF;
    }
    iwrap_active_connections++;
    add_mapped_connection(link_id, address, type, target);
    print_connection_map();
}

void my_iwrap_evt_no_carrier(uint8_t link_id, uint16_t error_code, const char *message) {
    if (iwrap_pending_call_link_id == link_id) {
        if (iwrap_pending_calls) iwrap_pending_calls--;
        if (iwrap_state == IWRAP_STATE_PENDING_CALL) iwrap_state = IWRAP_STATE_IDLE;
        iwrap_pending_call_link_id = 0xFF;
    }
    if (remove_mapped_connection(link_id) != 0xFF) {
        // only update and reprint if the connection was already mapped
        // (i.e. not already closed or a failed outgoing connection attempt)
        if (iwrap_active_connections) iwrap_active_connections--;
        print_connection_map();
    }
}

void my_iwrap_evt_pair(const iwrap_address_t *address, uint8_t key_type, const uint8_t *link_key) {
    // request pair list again (could be a new pair, or updated pair, or new + overwritten pair)
    iwrap_send_command("SET BT PAIR", iwrap_mode);
    iwrap_state = IWRAP_STATE_PENDING_SET;
}

void my_iwrap_evt_ready() {
    iwrap_state = IWRAP_STATE_UNKNOWN;
}

void my_iwrap_evt_ring(uint8_t link_id, const iwrap_address_t *address, uint8_t channel, const char *profile) {
    add_mapped_connection(link_id, address, profile, channel);
    print_connection_map();
}

/* ============================================================================
 * GENERAL HELPER FUNCTIONS
 * ========================================================================= */

uint8_t find_pairing_from_mac(const iwrap_address_t *mac) {
    uint8_t i;
    for (i = 0; i < iwrap_pairings; i++) {
        if (memcmp(&(iwrap_connection_map[i] -> mac), mac, sizeof(iwrap_address_t)) == 0) return i;
    }
    return i >= iwrap_pairings ? 0xFF : i;
}

uint8_t find_pairing_from_link_id(uint8_t link_id) {
    // NOTE: This implementation walks through the iwrap_connection_t struct memory
    // starting at the first link ID (A2DP #1) and going all the way to the end (if
    // necessary). This means it is contingent on the structure keeping this memory
    // arrangement, where the last set of whatever is contained there is always the
    // set of link IDs.
    
    uint8_t i, j, *idptr;
    for (i = 0; i < iwrap_pairings; i++) {
        idptr = &(iwrap_connection_map[i] -> link_a2dp1);
        for (j = 6; j < sizeof(iwrap_connection_t); j++, idptr++) if (idptr[0] == link_id) return i;
    }
    return 0xFF;
}

void add_mapped_connection(uint8_t link_id, const iwrap_address_t *addr, const char *mode, uint16_t channel) {
    uint8_t pairing_index = find_pairing_from_mac(addr);

    // make sure we found a match (we SHOULD always match something, if we properly parsed the pairing data first)
    if (pairing_index == 0xFF) return; // uh oh

    // updated connected device count and overall active link count for this device
    if (!iwrap_connection_map[pairing_index] -> active_links) iwrap_connected_devices++;
    iwrap_connection_map[pairing_index] -> active_links++;

    // add link ID to connection map
    if (strcmp(mode, "A2DP") == 0) {
        if (iwrap_connection_map[pairing_index] -> link_a2dp1 == 0xFF) {
            iwrap_connection_map[pairing_index] -> link_a2dp1 = link_id;
        } else {
            iwrap_connection_map[pairing_index] -> link_a2dp2 = link_id;
        }
    } else if (strcmp(mode, "AVRCP") == 0) {
        iwrap_connection_map[pairing_index] -> link_avrcp = link_id;
    } else if (strcmp(mode, "HFP") == 0) {
        iwrap_connection_map[pairing_index] -> link_hfp = link_id;
    } else if (strcmp(mode, "HFPAG") == 0) {
        iwrap_connection_map[pairing_index] -> link_hfpag = link_id;
    } else if (strcmp(mode, "HID") == 0) {
        if (channel == 0x11) {
            iwrap_connection_map[pairing_index] -> link_hid_control = link_id;
        } else {
            iwrap_connection_map[pairing_index] -> link_hid_interrupt = link_id;
        }
    } else if (strcmp(mode, "HSP") == 0) {
        iwrap_connection_map[pairing_index] -> link_hsp = link_id;
    } else if (strcmp(mode, "HSPAG") == 0) {
        iwrap_connection_map[pairing_index] -> link_hspag = link_id;
    } else if (strcmp(mode, "IAP") == 0) {
        iwrap_connection_map[pairing_index] -> link_iap = link_id;
    } else if (strcmp(mode, "RFCOMM") == 0) {
        // probably SPP, possibly other RFCOMM-based connections
        if (channel == 1) {
            iwrap_connection_map[pairing_index] -> link_spp = link_id;
        //} else {
            //printf("RFCOMM link on channel %d, profile unknown\n", channel);
        }
    }
}

uint8_t remove_mapped_connection(uint8_t link_id) {
    uint8_t i;
    for (i = 0; i < iwrap_pairings; i++) {
        if (iwrap_connection_map[i] -> link_a2dp1 == link_id) { iwrap_connection_map[i] -> link_a2dp1 = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_a2dp2 == link_id) { iwrap_connection_map[i] -> link_a2dp2 = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_avrcp == link_id) { iwrap_connection_map[i] -> link_avrcp = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hfp == link_id) { iwrap_connection_map[i] -> link_hfp = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hfpag == link_id) { iwrap_connection_map[i] -> link_hfpag = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hid_control == link_id) { iwrap_connection_map[i] -> link_hid_control = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hid_interrupt == link_id) { iwrap_connection_map[i] -> link_hid_interrupt = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hsp == link_id) { iwrap_connection_map[i] -> link_hsp = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_hspag == link_id) { iwrap_connection_map[i] -> link_hspag = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_iap == link_id) { iwrap_connection_map[i] -> link_iap = 0xFF; break; }
        if (iwrap_connection_map[i] -> link_spp == link_id) { iwrap_connection_map[i] -> link_spp = 0xFF; break; }
    }
    
    // check to make sure we found the link ID in the map
    if (i < iwrap_pairings) {
        // updated connected device count and overall active link count for this device
        if (iwrap_connection_map[i] -> active_links) {
            iwrap_connection_map[i] -> active_links--;
            if (!iwrap_connection_map[i] -> active_links) iwrap_connected_devices--;
        }
        return i;
    }
    
    // not found, return 0xFF
    return 0xFF;
}

/* ============================================================================
 * PLATFORM-SPECIFIC HELPER FUNCTIONS
 * ========================================================================= */

#if defined(PLATFORM_ARDUINO_UNO)
    int serial_out(const char *str) {
        // debug output to host goes through hardware serial
        return Serial.print(str);
    }
    int serial_out(const __FlashStringHelper *str) {
        // debug output to host goes through hardware serial
        return Serial.print(str);
    }
    int iwrap_out(int len, unsigned char *data) {
        // iWRAP output to module goes through software serial
        return mySerial.write(data, len);
    }
#elif defined(PLATFORM_TEENSY2)
    int serial_out(const char *str) {
        // debug output to host goes through Serial (USB)
        return Serial.print(str);
    }
    int serial_out(const __FlashStringHelper *str) {
        // debug output to host goes through Serial (USB)
        return Serial.print(str);
    }
    int iwrap_out(int len, unsigned char *data) {
        // iWRAP output to module goes through Serial1 (hardware UART)
        return Serial1.write(data, len);
    }
#endif
    
void print_connection_map() {
    char s[100];
    serial_out(F("==============================================================================\n"));
    sprintf(s, "Connection map (%d pairings, %d connected devices, %d total connections)\n", iwrap_pairings, iwrap_connected_devices, iwrap_active_connections);
    serial_out(s);
    serial_out(F("--------+-------+-------+-----+-------+-------+-----+-------+-----+-----+-----\n"));
    serial_out(F("Device\t|  A2DP | AVRCP | HFP | HFPAG |  HID  | HSP | HSPAG | IAP | SCO | SPP\n"));
    serial_out(F("--------+-------+-------+-----+-------+-------+-----+-------+-----+-----+-----\n"));
    uint8_t i;
    for (i = 0; i < iwrap_pairings; i++) {
        sprintf(s, "#%d (%d)\t| %2d/%2d |  %2d   |  %2d |   %2d  | %2d/%2d |  %2d |   %2d  |  %2d |  %2d |  %2d\n", i, iwrap_connection_map[i] -> active_links,
            (int8_t)iwrap_connection_map[i] -> link_a2dp1,
            (int8_t)iwrap_connection_map[i] -> link_a2dp2,
            (int8_t)iwrap_connection_map[i] -> link_avrcp,
            (int8_t)iwrap_connection_map[i] -> link_hfp,
            (int8_t)iwrap_connection_map[i] -> link_hfpag,
            (int8_t)iwrap_connection_map[i] -> link_hid_control,
            (int8_t)iwrap_connection_map[i] -> link_hid_interrupt,
            (int8_t)iwrap_connection_map[i] -> link_hsp,
            (int8_t)iwrap_connection_map[i] -> link_hspag,
            (int8_t)iwrap_connection_map[i] -> link_iap,
            (int8_t)iwrap_connection_map[i] -> link_sco,
            (int8_t)iwrap_connection_map[i] -> link_spp
            );
        serial_out(s);
    }
    serial_out(F("--------+-------+-------+-----+-------+-------+-----+-------+-----+-----+-----\n"));
}

void print_demo_menu() {
    serial_out(F("iWRAP generic demo menu\n"));
    serial_out(F("============================================================\n"));
    serial_out(F("0: Reset module (pulses iWRAP module RESET pin high for 5ms)\n"));
    serial_out(F("1: Toggle round-robin autocall algorithm on or off\n"));
    serial_out(F("2: Make module discoverable and connectable (SET BT PAGE 3)\n"));
    serial_out(F("3: Make module undiscoverable, but connectable (SET BT PAGE 2)\n"));
    serial_out(F("4: Make module undiscoverable and unconnectable (SET BT PAGE 0)\n"));
}

void process_demo_choice(char b) {
    // check for RESET command ("0")
    if (b == '0') {
        serial_out("=> (0) Performing hardware reset on iWRAP module\n\n");
        digitalWrite(MODULE_RESET_PIN, HIGH);
        delay(5);
        digitalWrite(MODULE_RESET_PIN, LOW);
        iwrap_pending_commands = 0; // normally handled by the parser, but this is a hard reset
    } else if (b == '1') {
        if (iwrap_autocall_target) {
            serial_out("=> (1) Disabling round-robin autocall algorithm\n\n");
            iwrap_autocall_target = 0;
        } else {
            serial_out("=> (1) Enabling round-robin autocall algorithm\n\n");
            iwrap_autocall_target = 1;
        }
    } else if (b == '2') {
        serial_out("=> (2) Setting page mode to 3 (discoverable and connectable)\n\n");
        iwrap_send_command("SET BT PAGEMODE 3", iwrap_mode);
    } else if (b == '3') {
        serial_out("=> (3) Setting page mode to 2 (undiscoverable, but connectable)\n\n");
        iwrap_send_command("SET BT PAGEMODE 2", iwrap_mode);
    } else if (b == '4') {
        serial_out("=> (4) Setting page mode to 0 (undiscoverable and unconnectable)\n\n");
        iwrap_send_command("SET BT PAGEMODE 0", iwrap_mode);
    }
}