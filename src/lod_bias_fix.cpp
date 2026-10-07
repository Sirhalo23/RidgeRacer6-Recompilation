// Texture LOD bias: forced to zero on the Vulkan backend (Linux, Steam Deck).
//
// The SDK's Vulkan shader translator (v0.10.0) takes a texture's "result
// exponent bias" from the wrong word of the texture's settings: it reads bits
// 13-18 of word 4, where the mip LOD bias is kept, instead of bits 13-18 of
// word 3 (src/graphics/pipeline/shader/spirv_translator_fetch.cpp; the
// Direct3D 12 translator reads word 3). Every colour sampled from a texture
// that has a LOD bias is therefore multiplied by two to the power of a number
// made from that bias.
//
// Ridge Racer 6 sets a LOD bias of -1.0 on its track textures (road, terrain,
// buildings, sky) for a sharper picture. On Vulkan that turns into a factor of
// 2^-16: those textures come out black, while everything without a bias (cars,
// signs, the race display) looks right. Other bias values give other factors,
// which is why some trees turned white instead. Seen on Mesa's software driver
// and on a Steam Deck; found with RenderDoc (a road pixel's shader was given
// 0.666 by the texture and wrote 0.00001).
//
// The game sets the bias in one place, its D3D sampler-state function
//   sub_82256F00  SetSamplerState(D3DSAMP_MIPMAPLODBIAS): r3 = device,
//                 r4 = sampler, r5 = the bias as float bits
// the only code that writes that field. Here the bias is replaced with zero
// before the function stores it, so the wrongly read exponent is zero too.
// The cost is the sharpening itself: distant road texture is one mip level
// softer than on Windows, which the 16x anisotropic filtering the settings
// file asks for largely makes up for.
//
// Direct3D 12 (Windows) does not have the fault, so the default there is off.
// Remove this file once the SDK reads the right word.

#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc/context.h>

#include "generated/default/rr6_recomp_init.h"

#if defined(_WIN32)
#define RR6_ZERO_LOD_BIAS_DEFAULT false
#else
#define RR6_ZERO_LOD_BIAS_DEFAULT true
#endif

REXCVAR_DEFINE_BOOL(rr6_zero_lod_bias, RR6_ZERO_LOD_BIAS_DEFAULT, "RR6",
                    "Set every texture LOD bias the game asks for to zero. Needed on Vulkan "
                    "(Linux, Steam Deck), where the SDK turns a LOD bias into a brightness "
                    "factor and the track comes out black; not needed on Direct3D 12.");

// SetSamplerState(D3DSAMP_MIPMAPLODBIAS): r3 = device, r4 = sampler, r5 = float bits.
REX_HOOK_RAW(sub_82256F00) {
  if (REXCVAR_GET(rr6_zero_lod_bias)) {
    ctx.r5.u64 = 0;  // 0.0f
  }
  __imp__sub_82256F00(ctx, base);
}
