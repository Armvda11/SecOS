/* GPLv2 (c) Airbus */
OUTPUT_FORMAT("elf32-i386","elf32-i386","elf32-i386");
OUTPUT_ARCH("i386")

ENTRY(entry)

PHDRS
{
   phboot  PT_LOAD FLAGS (7);
   phstack PT_LOAD FLAGS (6);
   phsetup PT_LOAD FLAGS (7);
   phuser  PT_LOAD FLAGS (7);
}

SECTIONS
{
   /* Noyau placé à 3MB */
   . = 0x300000;

   .mbh      : ALIGN(4)      { KEEP(*(.mbh)) } : phboot
   .stack    : ALIGN(16)     { KEEP(*(.stack)) } : phstack

   __kernel_start__ = .;

   .idt_jmp  : ALIGN(16)     { KEEP(*(.idt_jmp)) } : phsetup

   /* Attraper aussi les sous-sections (.text.* etc.) */
   .text     : ALIGN(16)     { *(.text .text.*) } : phsetup
   .rodata   : ALIGN(16)     { *(.rodata .rodata.*) } : phsetup
   .data     : ALIGN(16)     { *(.data .data.*) } : phsetup
   .bss      : ALIGN(16)     { *(.bss .bss.* COMMON) } : phsetup

   /*
    * IMPORTANT :
    * On saute exactement à 4MB (frontière PDE 4MB) pour que le mapping pgd[1]
    * (VA 4..8MB) corresponde.
    */
   . = 0x400000;

   /*
    * Section user : alignée 4KB (pages), et KEEP pour éviter l’élimination
    * si la section n’est pas “référencée” selon la config.
    */
   .user : ALIGN(4096)
   {
      KEEP(*(.user))
      KEEP(*(.user.*))
   } : phuser

   __kernel_end__ = .;

   /DISCARD/ : { *(.note* .indent .comment) }
}

