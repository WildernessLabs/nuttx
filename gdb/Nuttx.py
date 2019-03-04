import gdb
import binascii
import struct

verbose = False

# This receives a base stack pointer and reads the register
# values saved in memory by the ARM processor and NuttX.
# See arch/arm/src/armv7-m/gnu/up_lazyexception.S for details.

class NuttxRegContext():
    def __init__(self, stack_top):
        self.stack_top = stack_top

        self.sw_regs = ["r2","r3","r4","r5","r6","r7","r8","r9","r10","r11","r14"]
        self.sw_regs_base = self.stack_top + len(self.sw_regs) * 4

        self.fpu_regs = [None] * 33
        self.fpu_regs_base = self.sw_regs_base + len(self.fpu_regs) * 4

        self.hw_regs = ["r0","r1","r2","r3","r12","lr","pc","xpsr"]
        self.hw_regs_base = self.fpu_regs_base + len(self.hw_regs) * 4

    def dump_registers(self):
        print("sw regs:")
        [self.dump_register(self.sw_regs_base, len(self.sw_regs) - i, reg)
            for i,reg in enumerate(self.sw_regs)]

        print("hw regs:")
        [self.dump_register(self.hw_regs_base, len(self.hw_regs) - i, reg)
            for i,reg in enumerate(self.hw_regs)]

        print("stack_top %s sw_regs_base %s fpu_regs_base %s hw_regs_base %s" 
            % (self.stack_top, self.sw_regs_base, self.fpu_regs_base,
               self.hw_regs_base))

    def read_hw_register(self, reg):
        i = self.hw_regs.index(reg)
        return read_memory_word_offset(self.hw_regs_base, len(self.hw_regs) - i)

    def dump_register(self, base, offset, reg):
        hex, addr = read_memory_word_offset(base, offset)
        print("[%s/%s]: 0x%s" % (reg, addr, hex))

# This keeps a saved copy of the ARM processor registers state.
class ARMRegContext():
    def __init__(self, frame):
        self.frame = frame
        self.regs = [ "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7", "r8",
                      "r9", "r10", "r11", "r12", "sp", "lr", "pc", "xpsr",
                      "msp", "psp", "control", "faultmask", "basepri",
                      "primask", "fpscr" ]
        self.values = [None] * len(self.regs)

    # Saves the current values of the registers.
    def save_registers(self):
        for i,reg in enumerate(self.regs):
            value = long(self.frame.read_register(reg))
            self.values[i] = value

    # Restores the previously saved values of the registers.
    def restore_registers(self):
        for i,reg in enumerate(self.regs):
            cmd = "$%s = %s" % (reg, self.values[i])
            gdb.parse_and_eval("$%s = %s" % (reg, self.values[i]))

    def dump_registers(self):
        for i,reg in enumerate(self.regs):
            print("%s: %s" % (reg, self.values[i]))

class NuttxBacktrace(gdb.Command):
    def __init__(self):
        super(NuttxBacktrace, self).__init__("nx_bt", gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        try:
            # Save a copy of the current CPU context.
            self.ctx = ARMRegContext(gdb.newest_frame())
            self.ctx.save_registers()

            frame = gdb.selected_frame()

            i = 0
            while frame != None:
                if frame.pc() == 0:
                    break

                annotations = self.annotate_frame(frame)
                self.print_frame(frame, i, annotations)
                self.handle_frame(frame)
                i = i + 1

                # This can happen when we switch CPU context.
                if not frame.is_valid():
                    frame = gdb.selected_frame()
                else:
                    frame = frame.older()
        finally:
            self.ctx.restore_registers()

    def print_frame(self, frame, i, annotations):
        annotations_text = ""
        if annotations != None:
            annotations_text = "[%s]" % (annotations)

        sal = frame.find_sal()
        symtab = sal.symtab
        filename = symtab.filename if symtab else ""

        print("#%s 0x%s %s () at %s:%s %s" % (i, format_hex(sal.pc),
            frame.name(), filename, sal.line, annotations_text))

    def annotate_frame(self, frame):
        if frame.name() == None:
            return
        elif frame.name() == "up_svcall":
            return self.annotate_frame_svcall(frame)
        elif frame.name() == "exception_common":
            return self.annotate_frame_exception_common(frame)
        elif "sys_call" in frame.name():
            return self.annotate_frame_sys_call(frame)

    def annotate_frame_sys_call(self, frame):
        return "syscall"

    def annotate_frame_exception_common(self, frame):
            xpsr = frame.read_register("xpsr")
            ipsr = long(xpsr & 0x0000001f)
            stm_vectors = [ "IDLE_STACK", "__start", "stm32_nmi",
                "stm32_hardfault", "stm32_mpu", "stm32_busfault",
                "stm32_usagefaulf", "stm32_reserved", "stm32_reserved",
                "stm32_reserved", "stm32_reserved", "stm32_svcall",
                "stm32_dbgmonitor", "stm32_reserved", "stm32_pendsv",
                "stm32_systick"]
            assert ipsr < len(stm_vectors)
            return stm_vectors[ipsr]

    def annotate_frame_svcall(self, frame):
            regs = frame.read_var("context")
            ctx = NuttxRegContext(regs)
            cmd = long(ctx.read_hw_register("r0")[0])
            svcalls = ["SYS_save_context", "SYS_restore_context",
                       "SYS_switch_context", "SYS_syscall_return",
                       "SYS_task_start", "SYS_pthread_start",
                       "SYS_signal_handler", "SYS_signal_handler_return" ]
            if  cmd < len(svcalls):
                return svcalls[cmd]
            return
            # TODO: Add annotating support for non-core syscalls.

    def handle_frame(self, frame):
        if frame.name() == "exception_common":
            self.handle_frame_exception_common(frame)
        elif frame.name() == "dispatch_syscall":
            self.handle_frame_dispatch_syscall(frame)

    def handle_frame_dispatch_syscall(self, frame):
        # See default case of up_svcall.
        # It sets up the original frame return in the TCB xcp regs structure.
        # TODO: Handle CONFIG_SMP build if we support it in the future.
        nsyscalls = long(gdb.parse_and_eval(
            "((struct tcb_s *)g_readytorun.head)->xcp.nsyscalls"))

        CONFIG_SYS_NNEST = 2
        assert nsyscalls <= CONFIG_SYS_NNEST

        index = nsyscalls - 1
        sysreturn = long(gdb.parse_and_eval(
            "((struct tcb_s *)g_readytorun.head)->xcp.syscall[%d].sysreturn"
                % index))

        pc = sysreturn

        # How we get the LR value depends on where exactly we are stopped
        # inside dispatch_syscall. It can be saved on the stack or in the
        # LR register if we have not branched to the syscall stub yet.
        if frame.newer() != None:
            sp = frame.read_register("sp")
            lr =  read_memory_word(long(sp) + 12)
            sp = sp + 16
        else:
            # Need to take into account PC relative to dispatch_syscall
            print("Not yet implemented")

        #print("set $sp = 0x%s" % format_hex(sp))
        #print("set $lr = 0x%s" % lr)
        #print("set $pc = 0x%s" % format_hex(pc))

        gdb.parse_and_eval("$sp = 0x%s" % format_hex(sp))
        gdb.parse_and_eval("$lr = 0x%s" % lr)
        gdb.parse_and_eval("$pc = 0x%s" % format_hex(pc))

    def handle_frame_exception_common(self, frame):
            r4 = frame.read_register("r4")

            #  Get the stack pointer before exception handler.
            #  (8 HW regs) + (33 FPU regs) + (11 SW regs) * 4 bytes
            ctx_size = 208 # 32 + 132 + 44
            user_sp = r4 + ctx_size

            ctx = NuttxRegContext(r4)
            user_pc = ctx.read_hw_register("pc")[0]
            user_lr = ctx.read_hw_register("lr")[0]

            #print("set $sp = %s" % user_sp)
            #print("set $lr = 0x%s" % user_lr)
            #print("set $pc = 0x%s" % user_pc)

            gdb.parse_and_eval("$sp = %s" % (user_sp))
            gdb.parse_and_eval("$lr = 0x%s" % (user_lr))
            gdb.parse_and_eval("$pc = 0x%s" % (user_pc))

global_saved_regs_ctx = None

class NuttxSaveRegisters(gdb.Command):
    def __init__(self):
        super(NuttxSaveRegisters, self).__init__("nx_saveregs",
            gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global global_saved_regs_ctx
        print("Saving CPU registers context...")
        global_saved_regs_ctx = ARMRegContext(gdb.selected_frame())
        global_saved_regs_ctx.save_registers()

class NuttxRestoreRegisters(gdb.Command):
    def __init__(self):
        super(NuttxRestoreRegisters, self).__init__("nx_restoreregs",
            gdb.COMMAND_STACK)

    def invoke(self, arg, from_tty):
        global global_saved_regs_ctx
        if global_saved_regs_ctx == None:
            print("No saved CPU registers context was found.")
            return
        print("Restoring CPU registers context...")
        global_saved_regs_ctx.restore_registers()

def read_memory_word(addr):
    inferior = gdb.selected_inferior()
    mem = inferior.read_memory(addr, 4)
    return format_hex_swap32(mem)

def read_memory_word_offset(base, offset):
    addr = base - offset * 4
    inferior = gdb.selected_inferior()
    mem = inferior.read_memory(addr, 4)
    return format_hex_swap32(mem), addr

def format_hex(i):
    return binascii.hexlify(struct.pack(">I", i))

def format_hex_swap32(i):
    return binascii.hexlify(struct.pack("<I", struct.unpack(">I", i)[0]))

NuttxBacktrace()
NuttxSaveRegisters()
NuttxRestoreRegisters()