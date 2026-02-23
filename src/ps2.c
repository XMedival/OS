#include "ps2.h"
#include "print.h"
#include "x86.h"

static void ps2_wait_write(void) {
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL);
}

static void ps2_wait_read(void) {
    while (!(inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL));
}

void ps2_init(void) {
    // Enable keyboard interrupt (bit 0) in controller config
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    ps2_wait_read();
    u8 config = inb(PS2_DATA_PORT);
    config |= 0x01;  // keyboard interrupt
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);
    puts("[ OK ] Keyboard enabled\r\n");

    // Enable PS/2 port 2 (mouse)
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_PORT2);

    // Enable aux (mouse) interrupt (bit 1) in controller config
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    ps2_wait_read();
    config = inb(PS2_DATA_PORT);
    config |= 0x02;  // mouse interrupt
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_wait_write();
    outb(PS2_DATA_PORT, config);

    // Send "enable data reporting" to the mouse
    ps2_wait_write();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_AUX);
    ps2_wait_write();
    outb(PS2_DATA_PORT, PS2_MOUSE_ENABLE);
    ps2_wait_read();
    inb(PS2_DATA_PORT);  // discard ACK (0xFA)
    puts("[ OK ] Mouse enabled\r\n");
}
