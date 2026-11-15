# sum_100.asm - Compute 1 + 2 + ... + 100 = 5050 (0x13BA)
# Matches the exact instruction stream and memory layout of blk_mem.coe

    LOAD   0xA0      # 0x00: ACC = MEM[0xA0] (0)
    STORE  0xA4      # 0x01: MEM[0xA4] (sum) = 0
    LOAD   0xA2      # 0x02: ACC = MEM[0xA2] (100)
    STORE  0xA3      # 0x03: MEM[0xA3] (i) = 100

    # Loop body (starts at PC = 0x04)
    LOAD   0xA4      # 0x04: ACC = sum
    ADD    0xA3      # 0x05: ACC = sum + i
    STORE  0xA4      # 0x06: sum = sum + i
    LOAD   0xA3      # 0x07: ACC = i
    SUB    0xA1      # 0x08: ACC = i - 1
    STORE  0xA3      # 0x09: i = i - 1
    JMPGEZ 0x04      # 0x0A: if ACC >= 0 (Sign == 0), jump to 0x04

    LOAD   0xA4      # 0x0B: ACC = final sum (5050 = 0x13BA)
    HALT   0x00      # 0x0C: Halt processor

# Data segment at 0xA0..0xA2
.word 0xA0 0x0000    # Constant 0
.word 0xA1 0x0001    # Constant 1 (decrement step)
.word 0xA2 0x0064    # Constant 100 (initial counter)
