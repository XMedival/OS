OUTPUT_FORMAT(elf64-x86-64)
ENTRY(kmain)

PHDRS {
      text PT_LOAD;
      rodata PT_LOAD;
      data PT_LOAD;
}

SECTIONS {
	 . = 0xFFFFFFFF80000000;
	 .text : { *(.text .text.*); } : text
	 . = ALIGN(CONSTANT(MAXPAGESIZE));
	 .rodata : { *(.rodata*); } : rodata
	 . = ALIGN(CONSTANT(MAXPAGESIZE));
	 .data : { *(.data); } : data
	 . = ALIGN(CONSTANT(MAXPAGESIZE));
	 .bss : { *(COMMON) *(.bss*); } : data
}
