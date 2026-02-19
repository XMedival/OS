#ifndef PS2_H
#define PS2_H

// PS/2 Controller Ports
#define PS2_DATA_PORT      0x60  // Data port (read/write)
#define PS2_STATUS_PORT    0x64  // Status register (read)
#define PS2_CMD_PORT       0x64  // Command register (write)

// PS/2 Status Register bits
#define PS2_STATUS_OUTPUT_FULL  (1 << 0)  // Output buffer full (data available)
#define PS2_STATUS_INPUT_FULL   (1 << 1)  // Input buffer full (don't write)
#define PS2_STATUS_SYSTEM       (1 << 2)  // System flag
#define PS2_STATUS_CMD_DATA     (1 << 3)  // 0=data, 1=command
#define PS2_STATUS_TIMEOUT      (1 << 6)  // Timeout error
#define PS2_STATUS_PARITY       (1 << 7)  // Parity error

// PS/2 Controller Commands
#define PS2_CMD_READ_CONFIG     0x20
#define PS2_CMD_WRITE_CONFIG    0x60
#define PS2_CMD_DISABLE_PORT2   0xA7
#define PS2_CMD_ENABLE_PORT2    0xA8
#define PS2_CMD_TEST_PORT2      0xA9
#define PS2_CMD_TEST_CTRL       0xAA
#define PS2_CMD_TEST_PORT1      0xAB
#define PS2_CMD_DISABLE_PORT1   0xAD
#define PS2_CMD_ENABLE_PORT1    0xAE
#define PS2_CMD_READ_OUTPUT     0xD0
#define PS2_CMD_WRITE_OUTPUT    0xD1

// Keyboard Scancode constants
#define SCANCODE_RELEASE_BIT    0x80  // Bit set for key release in set 1

#endif
