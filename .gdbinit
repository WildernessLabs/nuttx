
source gdb/Nuttx.py
source gdb/Nuttx_Tasks.py

set confirm off
file nuttx
add-symbol-file -readnow nuttx
add-symbol-file -readnow configs/stm32f777zit6-meadow/kernel/nuttx_user.elf
set confirm on
set output-radix 16
target remote :4242
