"""Project-local x87 correction for the shared pcrecomp lifter.

FCOM/FUCOM/FTST update the x87 status word, not integer EFLAGS. MW's
0x81CDC6 sound routine schedules CMP channel,-1; FUCOMPP; JNE: replacing
the CMP flags sends the all-channel sentinel through a single-channel write,
overwriting its effects callback at voice+0x118 with a floating-point gain.
Keep the integer lazy flags until TEST/SAHF explicitly transfers the status.
"""
from lift32 import Lifter as BaseLifter


class Lifter(BaseLifter):
    def _lift_instruction(self, insn):
        # R_System::determineProcessorCaps (rlmfc) guarda los registros con 66 60.
        if insn.mnemonic == 'pushaw':
            return ['{ uint32_t _sp = esp; esp -= 16; '
                    'MEM16(esp + 14) = (uint16_t)eax; MEM16(esp + 12) = (uint16_t)ecx; '
                    'MEM16(esp + 10) = (uint16_t)edx; MEM16(esp + 8) = (uint16_t)ebx; '
                    'MEM16(esp + 6) = (uint16_t)_sp; MEM16(esp + 4) = (uint16_t)ebp; '
                    'MEM16(esp + 2) = (uint16_t)esi; MEM16(esp) = (uint16_t)edi; }']
        if insn.mnemonic == 'popaw':
            return ['edi = (edi & 0xFFFF0000u) | MEM16(esp); esi = (esi & 0xFFFF0000u) | MEM16(esp + 2); '
                    'ebp = (ebp & 0xFFFF0000u) | MEM16(esp + 4); ebx = (ebx & 0xFFFF0000u) | MEM16(esp + 8); '
                    'edx = (edx & 0xFFFF0000u) | MEM16(esp + 10); ecx = (ecx & 0xFFFF0000u) | MEM16(esp + 12); '
                    'eax = (eax & 0xFFFF0000u) | MEM16(esp + 14); esp += 16;']
        if insn.mnemonic in ('mov', 'lea') and insn.op_str.startswith('esp,'):
            return list(super()._lift_instruction(insn)) + ['RECOMP_SEH_ESP_MOVED();']
        if insn.mnemonic not in {'fcom', 'fcomp', 'fcompp',
                                 'fucom', 'fucomp', 'fucompp', 'ftst'}:
            return super()._lift_instruction(insn)
        state, seq = self._flag_state, self._flag_seq
        lines = super()._lift_instruction(insn)
        self._flag_state, self._flag_seq = state, seq
        return [line for line in lines
                if line != '_flag_a = (uint32_t)_fpu_cmp; _flag_b = 0;']
