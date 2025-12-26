/* GPLv2 (c) Airbus */
OUTPUT_FORMAT("elf32-i386","elf32-i386","elf32-i386");
OUTPUT_ARCH("i386")

ENTRY(entry)

PHDRS
{
   phboot  PT_LOAD FLAGS (7);
   phstack PT_LOAD FLAGS (6);
   phsetup PT_LOAD FLAGS (7);
   phuser  PT_LOAD FLAGS (7);  /* <--- NOUVEAU : Segment pour l'utilisateur */
}

SECTIONS
{
   . = 0x300000;
   .mbh      : { KEEP(*(.mbh)) . = ALIGN(4);      } : phboot
   .stack    : { KEEP(*(.stack))                  } : phstack

   __kernel_start__ = .;

   .idt_jmp  : { KEEP(*(.idt_jmp))                } : phsetup
   .text     : { *(.text)                         } : phsetup
   .rodata   : { *(.rodata)                       } : phsetup
   .data     : { *(.data)                         } : phsetup
   .bss      : { *(.bss COMMON)                   } : phsetup

   /* === C'EST ICI QUE TOUT SE JOUE === */
   /* On avance à 4MB (0x400000) pour isoler le code utilisateur */
   . = 0x400000;
   
   /* On place la section .user ici physiquement */
   .user     : { *(.user)                         } : phuser

   __kernel_end__ = .;
   /DISCARD/ : { *(.note* .indent .comment)       } : phsetup
}
