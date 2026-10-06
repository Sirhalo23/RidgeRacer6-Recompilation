// Hand-corrected versions of two recompiled functions.
//
// Both end in "lwzx r11,r10,r11; mtctr r11; bctr" through a table of FUNCTION
// POINTERS in .data (0x8236D1B0 / 0x8236D1E8) - an indirect tail call. Because
// each table happens to contain a pointer back to the function itself, the
// analyser mistakes it for a switch jump table and emits a switch that traps
// (first Windows run died here with STATUS_ILLEGAL_INSTRUCTION in sub_8217D288).
// The bodies below are the generated translation with the bogus switch replaced
// by an indirect call. Being strong symbols they replace the weak generated ones.
//
// tools/find_dispatch_tables.py lists every table-dispatch site so this can be
// re-checked if the manifest changes.

#include <rex/hook.h>

#include "generated/default/rr6_recomp_init.h"

REX_HOOK_RAW(sub_8217C938) {
	REX_FUNC_PROLOGUE();
	// lwz r10,32(r3)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 32);
	// lis r11,-32201
	ctx.r11.s64 = -2110324736;
	// lwz r9,12(r4)
	ctx.r9.u64 = REX_LOAD_U32(ctx.r4.u32 + 12);
	// addi r11,r11,-11800
	ctx.r11.s64 = ctx.r11.s64 + -11800;
	// lwz r10,0(r10)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r10.u32 + 0);
	// add r4,r10,r9
	ctx.r4.u64 = ctx.r10.u64 + ctx.r9.u64;
	// lwz r10,0(r4)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);
	// rlwinm r10,r10,2,0,29
	ctx.r10.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 2) & 0xFFFFFFFC;
	// lwzx r11,r10,r11
	ctx.r11.u64 = REX_LOAD_U32(ctx.r10.u32 + ctx.r11.u32);
	// mtctr r11
	ctx.ctr.u64 = ctx.r11.u64;
	// bctr  (indirect tail call through the handler table, not a switch)
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}

REX_HOOK_RAW(sub_8217D288) {
	REX_FUNC_PROLOGUE();
	// lwz r10,32(r3)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r3.u32 + 32);
	// lis r11,-32201
	ctx.r11.s64 = -2110324736;
	// lwz r9,8(r4)
	ctx.r9.u64 = REX_LOAD_U32(ctx.r4.u32 + 8);
	// addi r11,r11,-11856
	ctx.r11.s64 = ctx.r11.s64 + -11856;
	// lwz r10,0(r10)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r10.u32 + 0);
	// add r4,r10,r9
	ctx.r4.u64 = ctx.r10.u64 + ctx.r9.u64;
	// lwz r10,0(r4)
	ctx.r10.u64 = REX_LOAD_U32(ctx.r4.u32 + 0);
	// rlwinm r10,r10,2,0,29
	ctx.r10.u64 = __builtin_rotateleft64(ctx.r10.u32 | (ctx.r10.u64 << 32), 2) & 0xFFFFFFFC;
	// lwzx r11,r10,r11
	ctx.r11.u64 = REX_LOAD_U32(ctx.r10.u32 + ctx.r11.u32);
	// mtctr r11
	ctx.ctr.u64 = ctx.r11.u64;
	// bctr  (indirect tail call through the handler table, not a switch)
	REX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}
