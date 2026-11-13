#include "x86_recomp_shared.h"

const uint32_t X86CodeBase = 0x11000u;
const uint32_t X86CodeEnd = 0x11000u;
const uint32_t X86ImageBase = 0x10000u;
const uint32_t X86ImageEnd = 0x12000u;
const uint32_t X86EntryPoint = 0x11000u;
const uint32_t X86KernelThunk = 0x11000u;

const X86FuncMapping X86FuncMappings[] = {
	{0, nullptr},
};

const uint32_t X86Continuations[] = {
	0,
};

X86_FUNC(__imp__sub_FFFF0000) { x86_trap(ctx, 0xFFFF0000u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0000, __imp__sub_FFFF0000)

X86_FUNC(__imp__sub_FFFF0001) { x86_trap(ctx, 0xFFFF0001u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0001, __imp__sub_FFFF0001)

X86_FUNC(__imp__sub_FFFF0002) { x86_trap(ctx, 0xFFFF0002u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0002, __imp__sub_FFFF0002)

X86_FUNC(__imp__sub_FFFF0003) { x86_trap(ctx, 0xFFFF0003u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0003, __imp__sub_FFFF0003)

X86_FUNC(__imp__sub_FFFF0004) { x86_trap(ctx, 0xFFFF0004u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0004, __imp__sub_FFFF0004)

X86_FUNC(__imp__sub_FFFF0005) { x86_trap(ctx, 0xFFFF0005u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0005, __imp__sub_FFFF0005)

X86_FUNC(__imp__sub_FFFF0006) { x86_trap(ctx, 0xFFFF0006u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0006, __imp__sub_FFFF0006)

X86_FUNC(__imp__sub_FFFF0007) { x86_trap(ctx, 0xFFFF0007u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0007, __imp__sub_FFFF0007)

X86_FUNC(__imp__sub_FFFF0008) { x86_trap(ctx, 0xFFFF0008u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0008, __imp__sub_FFFF0008)

X86_FUNC(__imp__sub_FFFF0009) { x86_trap(ctx, 0xFFFF0009u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0009, __imp__sub_FFFF0009)

X86_FUNC(__imp__sub_FFFF000A) { x86_trap(ctx, 0xFFFF000Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000A, __imp__sub_FFFF000A)

X86_FUNC(__imp__sub_FFFF000B) { x86_trap(ctx, 0xFFFF000Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000B, __imp__sub_FFFF000B)

X86_FUNC(__imp__sub_FFFF000C) { x86_trap(ctx, 0xFFFF000Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000C, __imp__sub_FFFF000C)

X86_FUNC(__imp__sub_FFFF000D) { x86_trap(ctx, 0xFFFF000Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000D, __imp__sub_FFFF000D)

X86_FUNC(__imp__sub_FFFF000E) { x86_trap(ctx, 0xFFFF000Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000E, __imp__sub_FFFF000E)

X86_FUNC(__imp__sub_FFFF000F) { x86_trap(ctx, 0xFFFF000Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF000F, __imp__sub_FFFF000F)

X86_FUNC(__imp__sub_FFFF0010) { x86_trap(ctx, 0xFFFF0010u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0010, __imp__sub_FFFF0010)

X86_FUNC(__imp__sub_FFFF0011) { x86_trap(ctx, 0xFFFF0011u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0011, __imp__sub_FFFF0011)

X86_FUNC(__imp__sub_FFFF0012) { x86_trap(ctx, 0xFFFF0012u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0012, __imp__sub_FFFF0012)

X86_FUNC(__imp__sub_FFFF0013) { x86_trap(ctx, 0xFFFF0013u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0013, __imp__sub_FFFF0013)

X86_FUNC(__imp__sub_FFFF0014) { x86_trap(ctx, 0xFFFF0014u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0014, __imp__sub_FFFF0014)

X86_FUNC(__imp__sub_FFFF0015) { x86_trap(ctx, 0xFFFF0015u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0015, __imp__sub_FFFF0015)

X86_FUNC(__imp__sub_FFFF0016) { x86_trap(ctx, 0xFFFF0016u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0016, __imp__sub_FFFF0016)

X86_FUNC(__imp__sub_FFFF0017) { x86_trap(ctx, 0xFFFF0017u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0017, __imp__sub_FFFF0017)

X86_FUNC(__imp__sub_FFFF0018) { x86_trap(ctx, 0xFFFF0018u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0018, __imp__sub_FFFF0018)

X86_FUNC(__imp__sub_FFFF0019) { x86_trap(ctx, 0xFFFF0019u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0019, __imp__sub_FFFF0019)

X86_FUNC(__imp__sub_FFFF001A) { x86_trap(ctx, 0xFFFF001Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001A, __imp__sub_FFFF001A)

X86_FUNC(__imp__sub_FFFF001B) { x86_trap(ctx, 0xFFFF001Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001B, __imp__sub_FFFF001B)

X86_FUNC(__imp__sub_FFFF001C) { x86_trap(ctx, 0xFFFF001Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001C, __imp__sub_FFFF001C)

X86_FUNC(__imp__sub_FFFF001D) { x86_trap(ctx, 0xFFFF001Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001D, __imp__sub_FFFF001D)

X86_FUNC(__imp__sub_FFFF001E) { x86_trap(ctx, 0xFFFF001Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001E, __imp__sub_FFFF001E)

X86_FUNC(__imp__sub_FFFF001F) { x86_trap(ctx, 0xFFFF001Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF001F, __imp__sub_FFFF001F)

X86_FUNC(__imp__sub_FFFF0020) { x86_trap(ctx, 0xFFFF0020u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0020, __imp__sub_FFFF0020)

X86_FUNC(__imp__sub_FFFF0021) { x86_trap(ctx, 0xFFFF0021u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0021, __imp__sub_FFFF0021)

X86_FUNC(__imp__sub_FFFF0022) { x86_trap(ctx, 0xFFFF0022u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0022, __imp__sub_FFFF0022)

X86_FUNC(__imp__sub_FFFF0023) { x86_trap(ctx, 0xFFFF0023u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0023, __imp__sub_FFFF0023)

X86_FUNC(__imp__sub_FFFF0024) { x86_trap(ctx, 0xFFFF0024u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0024, __imp__sub_FFFF0024)

X86_FUNC(__imp__sub_FFFF0025) { x86_trap(ctx, 0xFFFF0025u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0025, __imp__sub_FFFF0025)

X86_FUNC(__imp__sub_FFFF0026) { x86_trap(ctx, 0xFFFF0026u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0026, __imp__sub_FFFF0026)

X86_FUNC(__imp__sub_FFFF0027) { x86_trap(ctx, 0xFFFF0027u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0027, __imp__sub_FFFF0027)

X86_FUNC(__imp__sub_FFFF0028) { x86_trap(ctx, 0xFFFF0028u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0028, __imp__sub_FFFF0028)

X86_FUNC(__imp__sub_FFFF0029) { x86_trap(ctx, 0xFFFF0029u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0029, __imp__sub_FFFF0029)

X86_FUNC(__imp__sub_FFFF002A) { x86_trap(ctx, 0xFFFF002Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002A, __imp__sub_FFFF002A)

X86_FUNC(__imp__sub_FFFF002B) { x86_trap(ctx, 0xFFFF002Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002B, __imp__sub_FFFF002B)

X86_FUNC(__imp__sub_FFFF002C) { x86_trap(ctx, 0xFFFF002Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002C, __imp__sub_FFFF002C)

X86_FUNC(__imp__sub_FFFF002D) { x86_trap(ctx, 0xFFFF002Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002D, __imp__sub_FFFF002D)

X86_FUNC(__imp__sub_FFFF002E) { x86_trap(ctx, 0xFFFF002Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002E, __imp__sub_FFFF002E)

X86_FUNC(__imp__sub_FFFF002F) { x86_trap(ctx, 0xFFFF002Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF002F, __imp__sub_FFFF002F)

X86_FUNC(__imp__sub_FFFF0030) { x86_trap(ctx, 0xFFFF0030u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0030, __imp__sub_FFFF0030)

X86_FUNC(__imp__sub_FFFF0031) { x86_trap(ctx, 0xFFFF0031u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0031, __imp__sub_FFFF0031)

X86_FUNC(__imp__sub_FFFF0032) { x86_trap(ctx, 0xFFFF0032u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0032, __imp__sub_FFFF0032)

X86_FUNC(__imp__sub_FFFF0033) { x86_trap(ctx, 0xFFFF0033u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0033, __imp__sub_FFFF0033)

X86_FUNC(__imp__sub_FFFF0034) { x86_trap(ctx, 0xFFFF0034u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0034, __imp__sub_FFFF0034)

X86_FUNC(__imp__sub_FFFF0035) { x86_trap(ctx, 0xFFFF0035u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0035, __imp__sub_FFFF0035)

X86_FUNC(__imp__sub_FFFF0036) { x86_trap(ctx, 0xFFFF0036u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0036, __imp__sub_FFFF0036)

X86_FUNC(__imp__sub_FFFF0037) { x86_trap(ctx, 0xFFFF0037u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0037, __imp__sub_FFFF0037)

X86_FUNC(__imp__sub_FFFF0038) { x86_trap(ctx, 0xFFFF0038u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0038, __imp__sub_FFFF0038)

X86_FUNC(__imp__sub_FFFF0039) { x86_trap(ctx, 0xFFFF0039u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0039, __imp__sub_FFFF0039)

X86_FUNC(__imp__sub_FFFF003A) { x86_trap(ctx, 0xFFFF003Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003A, __imp__sub_FFFF003A)

X86_FUNC(__imp__sub_FFFF003B) { x86_trap(ctx, 0xFFFF003Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003B, __imp__sub_FFFF003B)

X86_FUNC(__imp__sub_FFFF003C) { x86_trap(ctx, 0xFFFF003Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003C, __imp__sub_FFFF003C)

X86_FUNC(__imp__sub_FFFF003D) { x86_trap(ctx, 0xFFFF003Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003D, __imp__sub_FFFF003D)

X86_FUNC(__imp__sub_FFFF003E) { x86_trap(ctx, 0xFFFF003Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003E, __imp__sub_FFFF003E)

X86_FUNC(__imp__sub_FFFF003F) { x86_trap(ctx, 0xFFFF003Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF003F, __imp__sub_FFFF003F)

X86_FUNC(__imp__sub_FFFF0040) { x86_trap(ctx, 0xFFFF0040u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0040, __imp__sub_FFFF0040)

X86_FUNC(__imp__sub_FFFF0041) { x86_trap(ctx, 0xFFFF0041u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0041, __imp__sub_FFFF0041)

X86_FUNC(__imp__sub_FFFF0042) { x86_trap(ctx, 0xFFFF0042u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0042, __imp__sub_FFFF0042)

X86_FUNC(__imp__sub_FFFF0043) { x86_trap(ctx, 0xFFFF0043u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0043, __imp__sub_FFFF0043)

X86_FUNC(__imp__sub_FFFF0044) { x86_trap(ctx, 0xFFFF0044u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0044, __imp__sub_FFFF0044)

X86_FUNC(__imp__sub_FFFF0045) { x86_trap(ctx, 0xFFFF0045u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0045, __imp__sub_FFFF0045)

X86_FUNC(__imp__sub_FFFF0046) { x86_trap(ctx, 0xFFFF0046u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0046, __imp__sub_FFFF0046)

X86_FUNC(__imp__sub_FFFF0047) { x86_trap(ctx, 0xFFFF0047u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0047, __imp__sub_FFFF0047)

X86_FUNC(__imp__sub_FFFF0048) { x86_trap(ctx, 0xFFFF0048u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0048, __imp__sub_FFFF0048)

X86_FUNC(__imp__sub_FFFF0049) { x86_trap(ctx, 0xFFFF0049u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0049, __imp__sub_FFFF0049)

X86_FUNC(__imp__sub_FFFF004A) { x86_trap(ctx, 0xFFFF004Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004A, __imp__sub_FFFF004A)

X86_FUNC(__imp__sub_FFFF004B) { x86_trap(ctx, 0xFFFF004Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004B, __imp__sub_FFFF004B)

X86_FUNC(__imp__sub_FFFF004C) { x86_trap(ctx, 0xFFFF004Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004C, __imp__sub_FFFF004C)

X86_FUNC(__imp__sub_FFFF004D) { x86_trap(ctx, 0xFFFF004Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004D, __imp__sub_FFFF004D)

X86_FUNC(__imp__sub_FFFF004E) { x86_trap(ctx, 0xFFFF004Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004E, __imp__sub_FFFF004E)

X86_FUNC(__imp__sub_FFFF004F) { x86_trap(ctx, 0xFFFF004Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF004F, __imp__sub_FFFF004F)

X86_FUNC(__imp__sub_FFFF0050) { x86_trap(ctx, 0xFFFF0050u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0050, __imp__sub_FFFF0050)

X86_FUNC(__imp__sub_FFFF0051) { x86_trap(ctx, 0xFFFF0051u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0051, __imp__sub_FFFF0051)

X86_FUNC(__imp__sub_FFFF0052) { x86_trap(ctx, 0xFFFF0052u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0052, __imp__sub_FFFF0052)

X86_FUNC(__imp__sub_FFFF0053) { x86_trap(ctx, 0xFFFF0053u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0053, __imp__sub_FFFF0053)

X86_FUNC(__imp__sub_FFFF0054) { x86_trap(ctx, 0xFFFF0054u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0054, __imp__sub_FFFF0054)

X86_FUNC(__imp__sub_FFFF0055) { x86_trap(ctx, 0xFFFF0055u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0055, __imp__sub_FFFF0055)

X86_FUNC(__imp__sub_FFFF0056) { x86_trap(ctx, 0xFFFF0056u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0056, __imp__sub_FFFF0056)

X86_FUNC(__imp__sub_FFFF0057) { x86_trap(ctx, 0xFFFF0057u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0057, __imp__sub_FFFF0057)

X86_FUNC(__imp__sub_FFFF0058) { x86_trap(ctx, 0xFFFF0058u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0058, __imp__sub_FFFF0058)

X86_FUNC(__imp__sub_FFFF0059) { x86_trap(ctx, 0xFFFF0059u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0059, __imp__sub_FFFF0059)

X86_FUNC(__imp__sub_FFFF005A) { x86_trap(ctx, 0xFFFF005Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005A, __imp__sub_FFFF005A)

X86_FUNC(__imp__sub_FFFF005B) { x86_trap(ctx, 0xFFFF005Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005B, __imp__sub_FFFF005B)

X86_FUNC(__imp__sub_FFFF005C) { x86_trap(ctx, 0xFFFF005Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005C, __imp__sub_FFFF005C)

X86_FUNC(__imp__sub_FFFF005D) { x86_trap(ctx, 0xFFFF005Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005D, __imp__sub_FFFF005D)

X86_FUNC(__imp__sub_FFFF005E) { x86_trap(ctx, 0xFFFF005Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005E, __imp__sub_FFFF005E)

X86_FUNC(__imp__sub_FFFF005F) { x86_trap(ctx, 0xFFFF005Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF005F, __imp__sub_FFFF005F)

X86_FUNC(__imp__sub_FFFF0060) { x86_trap(ctx, 0xFFFF0060u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0060, __imp__sub_FFFF0060)

X86_FUNC(__imp__sub_FFFF0061) { x86_trap(ctx, 0xFFFF0061u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0061, __imp__sub_FFFF0061)

X86_FUNC(__imp__sub_FFFF0062) { x86_trap(ctx, 0xFFFF0062u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0062, __imp__sub_FFFF0062)

X86_FUNC(__imp__sub_FFFF0063) { x86_trap(ctx, 0xFFFF0063u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0063, __imp__sub_FFFF0063)

X86_FUNC(__imp__sub_FFFF0064) { x86_trap(ctx, 0xFFFF0064u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0064, __imp__sub_FFFF0064)

X86_FUNC(__imp__sub_FFFF0065) { x86_trap(ctx, 0xFFFF0065u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0065, __imp__sub_FFFF0065)

X86_FUNC(__imp__sub_FFFF0066) { x86_trap(ctx, 0xFFFF0066u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0066, __imp__sub_FFFF0066)

X86_FUNC(__imp__sub_FFFF0067) { x86_trap(ctx, 0xFFFF0067u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0067, __imp__sub_FFFF0067)

X86_FUNC(__imp__sub_FFFF0068) { x86_trap(ctx, 0xFFFF0068u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0068, __imp__sub_FFFF0068)

X86_FUNC(__imp__sub_FFFF0069) { x86_trap(ctx, 0xFFFF0069u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0069, __imp__sub_FFFF0069)

X86_FUNC(__imp__sub_FFFF006A) { x86_trap(ctx, 0xFFFF006Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006A, __imp__sub_FFFF006A)

X86_FUNC(__imp__sub_FFFF006B) { x86_trap(ctx, 0xFFFF006Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006B, __imp__sub_FFFF006B)

X86_FUNC(__imp__sub_FFFF006C) { x86_trap(ctx, 0xFFFF006Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006C, __imp__sub_FFFF006C)

X86_FUNC(__imp__sub_FFFF006D) { x86_trap(ctx, 0xFFFF006Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006D, __imp__sub_FFFF006D)

X86_FUNC(__imp__sub_FFFF006E) { x86_trap(ctx, 0xFFFF006Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006E, __imp__sub_FFFF006E)

X86_FUNC(__imp__sub_FFFF006F) { x86_trap(ctx, 0xFFFF006Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF006F, __imp__sub_FFFF006F)

X86_FUNC(__imp__sub_FFFF0070) { x86_trap(ctx, 0xFFFF0070u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0070, __imp__sub_FFFF0070)

X86_FUNC(__imp__sub_FFFF0071) { x86_trap(ctx, 0xFFFF0071u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0071, __imp__sub_FFFF0071)

X86_FUNC(__imp__sub_FFFF0072) { x86_trap(ctx, 0xFFFF0072u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0072, __imp__sub_FFFF0072)

X86_FUNC(__imp__sub_FFFF0073) { x86_trap(ctx, 0xFFFF0073u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0073, __imp__sub_FFFF0073)

X86_FUNC(__imp__sub_FFFF0074) { x86_trap(ctx, 0xFFFF0074u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0074, __imp__sub_FFFF0074)

X86_FUNC(__imp__sub_FFFF0075) { x86_trap(ctx, 0xFFFF0075u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0075, __imp__sub_FFFF0075)

X86_FUNC(__imp__sub_FFFF0076) { x86_trap(ctx, 0xFFFF0076u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0076, __imp__sub_FFFF0076)

X86_FUNC(__imp__sub_FFFF0077) { x86_trap(ctx, 0xFFFF0077u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0077, __imp__sub_FFFF0077)

X86_FUNC(__imp__sub_FFFF0078) { x86_trap(ctx, 0xFFFF0078u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0078, __imp__sub_FFFF0078)

X86_FUNC(__imp__sub_FFFF0079) { x86_trap(ctx, 0xFFFF0079u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0079, __imp__sub_FFFF0079)

X86_FUNC(__imp__sub_FFFF007A) { x86_trap(ctx, 0xFFFF007Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007A, __imp__sub_FFFF007A)

X86_FUNC(__imp__sub_FFFF007B) { x86_trap(ctx, 0xFFFF007Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007B, __imp__sub_FFFF007B)

X86_FUNC(__imp__sub_FFFF007C) { x86_trap(ctx, 0xFFFF007Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007C, __imp__sub_FFFF007C)

X86_FUNC(__imp__sub_FFFF007D) { x86_trap(ctx, 0xFFFF007Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007D, __imp__sub_FFFF007D)

X86_FUNC(__imp__sub_FFFF007E) { x86_trap(ctx, 0xFFFF007Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007E, __imp__sub_FFFF007E)

X86_FUNC(__imp__sub_FFFF007F) { x86_trap(ctx, 0xFFFF007Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF007F, __imp__sub_FFFF007F)

X86_FUNC(__imp__sub_FFFF0080) { x86_trap(ctx, 0xFFFF0080u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0080, __imp__sub_FFFF0080)

X86_FUNC(__imp__sub_FFFF0081) { x86_trap(ctx, 0xFFFF0081u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0081, __imp__sub_FFFF0081)

X86_FUNC(__imp__sub_FFFF0082) { x86_trap(ctx, 0xFFFF0082u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0082, __imp__sub_FFFF0082)

X86_FUNC(__imp__sub_FFFF0083) { x86_trap(ctx, 0xFFFF0083u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0083, __imp__sub_FFFF0083)

X86_FUNC(__imp__sub_FFFF0084) { x86_trap(ctx, 0xFFFF0084u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0084, __imp__sub_FFFF0084)

X86_FUNC(__imp__sub_FFFF0085) { x86_trap(ctx, 0xFFFF0085u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0085, __imp__sub_FFFF0085)

X86_FUNC(__imp__sub_FFFF0086) { x86_trap(ctx, 0xFFFF0086u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0086, __imp__sub_FFFF0086)

X86_FUNC(__imp__sub_FFFF0087) { x86_trap(ctx, 0xFFFF0087u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0087, __imp__sub_FFFF0087)

X86_FUNC(__imp__sub_FFFF0088) { x86_trap(ctx, 0xFFFF0088u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0088, __imp__sub_FFFF0088)

X86_FUNC(__imp__sub_FFFF0089) { x86_trap(ctx, 0xFFFF0089u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0089, __imp__sub_FFFF0089)

X86_FUNC(__imp__sub_FFFF008A) { x86_trap(ctx, 0xFFFF008Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008A, __imp__sub_FFFF008A)

X86_FUNC(__imp__sub_FFFF008B) { x86_trap(ctx, 0xFFFF008Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008B, __imp__sub_FFFF008B)

X86_FUNC(__imp__sub_FFFF008C) { x86_trap(ctx, 0xFFFF008Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008C, __imp__sub_FFFF008C)

X86_FUNC(__imp__sub_FFFF008D) { x86_trap(ctx, 0xFFFF008Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008D, __imp__sub_FFFF008D)

X86_FUNC(__imp__sub_FFFF008E) { x86_trap(ctx, 0xFFFF008Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008E, __imp__sub_FFFF008E)

X86_FUNC(__imp__sub_FFFF008F) { x86_trap(ctx, 0xFFFF008Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF008F, __imp__sub_FFFF008F)

X86_FUNC(__imp__sub_FFFF0090) { x86_trap(ctx, 0xFFFF0090u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0090, __imp__sub_FFFF0090)

X86_FUNC(__imp__sub_FFFF0091) { x86_trap(ctx, 0xFFFF0091u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0091, __imp__sub_FFFF0091)

X86_FUNC(__imp__sub_FFFF0092) { x86_trap(ctx, 0xFFFF0092u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0092, __imp__sub_FFFF0092)

X86_FUNC(__imp__sub_FFFF0093) { x86_trap(ctx, 0xFFFF0093u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0093, __imp__sub_FFFF0093)

X86_FUNC(__imp__sub_FFFF0094) { x86_trap(ctx, 0xFFFF0094u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0094, __imp__sub_FFFF0094)

X86_FUNC(__imp__sub_FFFF0095) { x86_trap(ctx, 0xFFFF0095u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0095, __imp__sub_FFFF0095)

X86_FUNC(__imp__sub_FFFF0096) { x86_trap(ctx, 0xFFFF0096u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0096, __imp__sub_FFFF0096)

X86_FUNC(__imp__sub_FFFF0097) { x86_trap(ctx, 0xFFFF0097u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0097, __imp__sub_FFFF0097)

X86_FUNC(__imp__sub_FFFF0098) { x86_trap(ctx, 0xFFFF0098u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0098, __imp__sub_FFFF0098)

X86_FUNC(__imp__sub_FFFF0099) { x86_trap(ctx, 0xFFFF0099u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0099, __imp__sub_FFFF0099)

X86_FUNC(__imp__sub_FFFF009A) { x86_trap(ctx, 0xFFFF009Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009A, __imp__sub_FFFF009A)

X86_FUNC(__imp__sub_FFFF009B) { x86_trap(ctx, 0xFFFF009Bu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009B, __imp__sub_FFFF009B)

X86_FUNC(__imp__sub_FFFF009C) { x86_trap(ctx, 0xFFFF009Cu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009C, __imp__sub_FFFF009C)

X86_FUNC(__imp__sub_FFFF009D) { x86_trap(ctx, 0xFFFF009Du, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009D, __imp__sub_FFFF009D)

X86_FUNC(__imp__sub_FFFF009E) { x86_trap(ctx, 0xFFFF009Eu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009E, __imp__sub_FFFF009E)

X86_FUNC(__imp__sub_FFFF009F) { x86_trap(ctx, 0xFFFF009Fu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF009F, __imp__sub_FFFF009F)

X86_FUNC(__imp__sub_FFFF00A0) { x86_trap(ctx, 0xFFFF00A0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A0, __imp__sub_FFFF00A0)

X86_FUNC(__imp__sub_FFFF00A1) { x86_trap(ctx, 0xFFFF00A1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A1, __imp__sub_FFFF00A1)

X86_FUNC(__imp__sub_FFFF00A2) { x86_trap(ctx, 0xFFFF00A2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A2, __imp__sub_FFFF00A2)

X86_FUNC(__imp__sub_FFFF00A3) { x86_trap(ctx, 0xFFFF00A3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A3, __imp__sub_FFFF00A3)

X86_FUNC(__imp__sub_FFFF00A4) { x86_trap(ctx, 0xFFFF00A4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A4, __imp__sub_FFFF00A4)

X86_FUNC(__imp__sub_FFFF00A5) { x86_trap(ctx, 0xFFFF00A5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A5, __imp__sub_FFFF00A5)

X86_FUNC(__imp__sub_FFFF00A6) { x86_trap(ctx, 0xFFFF00A6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A6, __imp__sub_FFFF00A6)

X86_FUNC(__imp__sub_FFFF00A7) { x86_trap(ctx, 0xFFFF00A7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A7, __imp__sub_FFFF00A7)

X86_FUNC(__imp__sub_FFFF00A8) { x86_trap(ctx, 0xFFFF00A8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A8, __imp__sub_FFFF00A8)

X86_FUNC(__imp__sub_FFFF00A9) { x86_trap(ctx, 0xFFFF00A9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00A9, __imp__sub_FFFF00A9)

X86_FUNC(__imp__sub_FFFF00AA) { x86_trap(ctx, 0xFFFF00AAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AA, __imp__sub_FFFF00AA)

X86_FUNC(__imp__sub_FFFF00AB) { x86_trap(ctx, 0xFFFF00ABu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AB, __imp__sub_FFFF00AB)

X86_FUNC(__imp__sub_FFFF00AC) { x86_trap(ctx, 0xFFFF00ACu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AC, __imp__sub_FFFF00AC)

X86_FUNC(__imp__sub_FFFF00AD) { x86_trap(ctx, 0xFFFF00ADu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AD, __imp__sub_FFFF00AD)

X86_FUNC(__imp__sub_FFFF00AE) { x86_trap(ctx, 0xFFFF00AEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AE, __imp__sub_FFFF00AE)

X86_FUNC(__imp__sub_FFFF00AF) { x86_trap(ctx, 0xFFFF00AFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00AF, __imp__sub_FFFF00AF)

X86_FUNC(__imp__sub_FFFF00B0) { x86_trap(ctx, 0xFFFF00B0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B0, __imp__sub_FFFF00B0)

X86_FUNC(__imp__sub_FFFF00B1) { x86_trap(ctx, 0xFFFF00B1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B1, __imp__sub_FFFF00B1)

X86_FUNC(__imp__sub_FFFF00B2) { x86_trap(ctx, 0xFFFF00B2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B2, __imp__sub_FFFF00B2)

X86_FUNC(__imp__sub_FFFF00B3) { x86_trap(ctx, 0xFFFF00B3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B3, __imp__sub_FFFF00B3)

X86_FUNC(__imp__sub_FFFF00B4) { x86_trap(ctx, 0xFFFF00B4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B4, __imp__sub_FFFF00B4)

X86_FUNC(__imp__sub_FFFF00B5) { x86_trap(ctx, 0xFFFF00B5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B5, __imp__sub_FFFF00B5)

X86_FUNC(__imp__sub_FFFF00B6) { x86_trap(ctx, 0xFFFF00B6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B6, __imp__sub_FFFF00B6)

X86_FUNC(__imp__sub_FFFF00B7) { x86_trap(ctx, 0xFFFF00B7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B7, __imp__sub_FFFF00B7)

X86_FUNC(__imp__sub_FFFF00B8) { x86_trap(ctx, 0xFFFF00B8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B8, __imp__sub_FFFF00B8)

X86_FUNC(__imp__sub_FFFF00B9) { x86_trap(ctx, 0xFFFF00B9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00B9, __imp__sub_FFFF00B9)

X86_FUNC(__imp__sub_FFFF00BA) { x86_trap(ctx, 0xFFFF00BAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BA, __imp__sub_FFFF00BA)

X86_FUNC(__imp__sub_FFFF00BB) { x86_trap(ctx, 0xFFFF00BBu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BB, __imp__sub_FFFF00BB)

X86_FUNC(__imp__sub_FFFF00BC) { x86_trap(ctx, 0xFFFF00BCu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BC, __imp__sub_FFFF00BC)

X86_FUNC(__imp__sub_FFFF00BD) { x86_trap(ctx, 0xFFFF00BDu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BD, __imp__sub_FFFF00BD)

X86_FUNC(__imp__sub_FFFF00BE) { x86_trap(ctx, 0xFFFF00BEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BE, __imp__sub_FFFF00BE)

X86_FUNC(__imp__sub_FFFF00BF) { x86_trap(ctx, 0xFFFF00BFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00BF, __imp__sub_FFFF00BF)

X86_FUNC(__imp__sub_FFFF00C0) { x86_trap(ctx, 0xFFFF00C0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C0, __imp__sub_FFFF00C0)

X86_FUNC(__imp__sub_FFFF00C1) { x86_trap(ctx, 0xFFFF00C1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C1, __imp__sub_FFFF00C1)

X86_FUNC(__imp__sub_FFFF00C2) { x86_trap(ctx, 0xFFFF00C2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C2, __imp__sub_FFFF00C2)

X86_FUNC(__imp__sub_FFFF00C3) { x86_trap(ctx, 0xFFFF00C3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C3, __imp__sub_FFFF00C3)

X86_FUNC(__imp__sub_FFFF00C4) { x86_trap(ctx, 0xFFFF00C4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C4, __imp__sub_FFFF00C4)

X86_FUNC(__imp__sub_FFFF00C5) { x86_trap(ctx, 0xFFFF00C5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C5, __imp__sub_FFFF00C5)

X86_FUNC(__imp__sub_FFFF00C6) { x86_trap(ctx, 0xFFFF00C6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C6, __imp__sub_FFFF00C6)

X86_FUNC(__imp__sub_FFFF00C7) { x86_trap(ctx, 0xFFFF00C7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C7, __imp__sub_FFFF00C7)

X86_FUNC(__imp__sub_FFFF00C8) { x86_trap(ctx, 0xFFFF00C8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C8, __imp__sub_FFFF00C8)

X86_FUNC(__imp__sub_FFFF00C9) { x86_trap(ctx, 0xFFFF00C9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00C9, __imp__sub_FFFF00C9)

X86_FUNC(__imp__sub_FFFF00CA) { x86_trap(ctx, 0xFFFF00CAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CA, __imp__sub_FFFF00CA)

X86_FUNC(__imp__sub_FFFF00CB) { x86_trap(ctx, 0xFFFF00CBu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CB, __imp__sub_FFFF00CB)

X86_FUNC(__imp__sub_FFFF00CC) { x86_trap(ctx, 0xFFFF00CCu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CC, __imp__sub_FFFF00CC)

X86_FUNC(__imp__sub_FFFF00CD) { x86_trap(ctx, 0xFFFF00CDu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CD, __imp__sub_FFFF00CD)

X86_FUNC(__imp__sub_FFFF00CE) { x86_trap(ctx, 0xFFFF00CEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CE, __imp__sub_FFFF00CE)

X86_FUNC(__imp__sub_FFFF00CF) { x86_trap(ctx, 0xFFFF00CFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00CF, __imp__sub_FFFF00CF)

X86_FUNC(__imp__sub_FFFF00D0) { x86_trap(ctx, 0xFFFF00D0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D0, __imp__sub_FFFF00D0)

X86_FUNC(__imp__sub_FFFF00D1) { x86_trap(ctx, 0xFFFF00D1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D1, __imp__sub_FFFF00D1)

X86_FUNC(__imp__sub_FFFF00D2) { x86_trap(ctx, 0xFFFF00D2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D2, __imp__sub_FFFF00D2)

X86_FUNC(__imp__sub_FFFF00D3) { x86_trap(ctx, 0xFFFF00D3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D3, __imp__sub_FFFF00D3)

X86_FUNC(__imp__sub_FFFF00D4) { x86_trap(ctx, 0xFFFF00D4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D4, __imp__sub_FFFF00D4)

X86_FUNC(__imp__sub_FFFF00D5) { x86_trap(ctx, 0xFFFF00D5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D5, __imp__sub_FFFF00D5)

X86_FUNC(__imp__sub_FFFF00D6) { x86_trap(ctx, 0xFFFF00D6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D6, __imp__sub_FFFF00D6)

X86_FUNC(__imp__sub_FFFF00D7) { x86_trap(ctx, 0xFFFF00D7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D7, __imp__sub_FFFF00D7)

X86_FUNC(__imp__sub_FFFF00D8) { x86_trap(ctx, 0xFFFF00D8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D8, __imp__sub_FFFF00D8)

X86_FUNC(__imp__sub_FFFF00D9) { x86_trap(ctx, 0xFFFF00D9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00D9, __imp__sub_FFFF00D9)

X86_FUNC(__imp__sub_FFFF00DA) { x86_trap(ctx, 0xFFFF00DAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DA, __imp__sub_FFFF00DA)

X86_FUNC(__imp__sub_FFFF00DB) { x86_trap(ctx, 0xFFFF00DBu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DB, __imp__sub_FFFF00DB)

X86_FUNC(__imp__sub_FFFF00DC) { x86_trap(ctx, 0xFFFF00DCu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DC, __imp__sub_FFFF00DC)

X86_FUNC(__imp__sub_FFFF00DD) { x86_trap(ctx, 0xFFFF00DDu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DD, __imp__sub_FFFF00DD)

X86_FUNC(__imp__sub_FFFF00DE) { x86_trap(ctx, 0xFFFF00DEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DE, __imp__sub_FFFF00DE)

X86_FUNC(__imp__sub_FFFF00DF) { x86_trap(ctx, 0xFFFF00DFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00DF, __imp__sub_FFFF00DF)

X86_FUNC(__imp__sub_FFFF00E0) { x86_trap(ctx, 0xFFFF00E0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E0, __imp__sub_FFFF00E0)

X86_FUNC(__imp__sub_FFFF00E1) { x86_trap(ctx, 0xFFFF00E1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E1, __imp__sub_FFFF00E1)

X86_FUNC(__imp__sub_FFFF00E2) { x86_trap(ctx, 0xFFFF00E2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E2, __imp__sub_FFFF00E2)

X86_FUNC(__imp__sub_FFFF00E3) { x86_trap(ctx, 0xFFFF00E3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E3, __imp__sub_FFFF00E3)

X86_FUNC(__imp__sub_FFFF00E4) { x86_trap(ctx, 0xFFFF00E4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E4, __imp__sub_FFFF00E4)

X86_FUNC(__imp__sub_FFFF00E5) { x86_trap(ctx, 0xFFFF00E5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E5, __imp__sub_FFFF00E5)

X86_FUNC(__imp__sub_FFFF00E6) { x86_trap(ctx, 0xFFFF00E6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E6, __imp__sub_FFFF00E6)

X86_FUNC(__imp__sub_FFFF00E7) { x86_trap(ctx, 0xFFFF00E7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E7, __imp__sub_FFFF00E7)

X86_FUNC(__imp__sub_FFFF00E8) { x86_trap(ctx, 0xFFFF00E8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E8, __imp__sub_FFFF00E8)

X86_FUNC(__imp__sub_FFFF00E9) { x86_trap(ctx, 0xFFFF00E9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00E9, __imp__sub_FFFF00E9)

X86_FUNC(__imp__sub_FFFF00EA) { x86_trap(ctx, 0xFFFF00EAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00EA, __imp__sub_FFFF00EA)

X86_FUNC(__imp__sub_FFFF00EB) { x86_trap(ctx, 0xFFFF00EBu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00EB, __imp__sub_FFFF00EB)

X86_FUNC(__imp__sub_FFFF00EC) { x86_trap(ctx, 0xFFFF00ECu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00EC, __imp__sub_FFFF00EC)

X86_FUNC(__imp__sub_FFFF00ED) { x86_trap(ctx, 0xFFFF00EDu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00ED, __imp__sub_FFFF00ED)

X86_FUNC(__imp__sub_FFFF00EE) { x86_trap(ctx, 0xFFFF00EEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00EE, __imp__sub_FFFF00EE)

X86_FUNC(__imp__sub_FFFF00EF) { x86_trap(ctx, 0xFFFF00EFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00EF, __imp__sub_FFFF00EF)

X86_FUNC(__imp__sub_FFFF00F0) { x86_trap(ctx, 0xFFFF00F0u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F0, __imp__sub_FFFF00F0)

X86_FUNC(__imp__sub_FFFF00F1) { x86_trap(ctx, 0xFFFF00F1u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F1, __imp__sub_FFFF00F1)

X86_FUNC(__imp__sub_FFFF00F2) { x86_trap(ctx, 0xFFFF00F2u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F2, __imp__sub_FFFF00F2)

X86_FUNC(__imp__sub_FFFF00F3) { x86_trap(ctx, 0xFFFF00F3u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F3, __imp__sub_FFFF00F3)

X86_FUNC(__imp__sub_FFFF00F4) { x86_trap(ctx, 0xFFFF00F4u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F4, __imp__sub_FFFF00F4)

X86_FUNC(__imp__sub_FFFF00F5) { x86_trap(ctx, 0xFFFF00F5u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F5, __imp__sub_FFFF00F5)

X86_FUNC(__imp__sub_FFFF00F6) { x86_trap(ctx, 0xFFFF00F6u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F6, __imp__sub_FFFF00F6)

X86_FUNC(__imp__sub_FFFF00F7) { x86_trap(ctx, 0xFFFF00F7u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F7, __imp__sub_FFFF00F7)

X86_FUNC(__imp__sub_FFFF00F8) { x86_trap(ctx, 0xFFFF00F8u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F8, __imp__sub_FFFF00F8)

X86_FUNC(__imp__sub_FFFF00F9) { x86_trap(ctx, 0xFFFF00F9u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00F9, __imp__sub_FFFF00F9)

X86_FUNC(__imp__sub_FFFF00FA) { x86_trap(ctx, 0xFFFF00FAu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FA, __imp__sub_FFFF00FA)

X86_FUNC(__imp__sub_FFFF00FB) { x86_trap(ctx, 0xFFFF00FBu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FB, __imp__sub_FFFF00FB)

X86_FUNC(__imp__sub_FFFF00FC) { x86_trap(ctx, 0xFFFF00FCu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FC, __imp__sub_FFFF00FC)

X86_FUNC(__imp__sub_FFFF00FD) { x86_trap(ctx, 0xFFFF00FDu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FD, __imp__sub_FFFF00FD)

X86_FUNC(__imp__sub_FFFF00FE) { x86_trap(ctx, 0xFFFF00FEu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FE, __imp__sub_FFFF00FE)

X86_FUNC(__imp__sub_FFFF00FF) { x86_trap(ctx, 0xFFFF00FFu, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF00FF, __imp__sub_FFFF00FF)

X86_FUNC(__imp__sub_FFFF0100) { x86_trap(ctx, 0xFFFF0100u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0100, __imp__sub_FFFF0100)

X86_FUNC(__imp__sub_FFFF0101) { x86_trap(ctx, 0xFFFF0101u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0101, __imp__sub_FFFF0101)

X86_FUNC(__imp__sub_FFFF0102) { x86_trap(ctx, 0xFFFF0102u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0102, __imp__sub_FFFF0102)

X86_FUNC(__imp__sub_FFFF0103) { x86_trap(ctx, 0xFFFF0103u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0103, __imp__sub_FFFF0103)

X86_FUNC(__imp__sub_FFFF0104) { x86_trap(ctx, 0xFFFF0104u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0104, __imp__sub_FFFF0104)

X86_FUNC(__imp__sub_FFFF0105) { x86_trap(ctx, 0xFFFF0105u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0105, __imp__sub_FFFF0105)

X86_FUNC(__imp__sub_FFFF0106) { x86_trap(ctx, 0xFFFF0106u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0106, __imp__sub_FFFF0106)

X86_FUNC(__imp__sub_FFFF0107) { x86_trap(ctx, 0xFFFF0107u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0107, __imp__sub_FFFF0107)

X86_FUNC(__imp__sub_FFFF0108) { x86_trap(ctx, 0xFFFF0108u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0108, __imp__sub_FFFF0108)

X86_FUNC(__imp__sub_FFFF0109) { x86_trap(ctx, 0xFFFF0109u, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF0109, __imp__sub_FFFF0109)

X86_FUNC(__imp__sub_FFFF010A) { x86_trap(ctx, 0xFFFF010Au, "not in this image"); }
X86_WEAK_FUNC(sub_FFFF010A, __imp__sub_FFFF010A)
