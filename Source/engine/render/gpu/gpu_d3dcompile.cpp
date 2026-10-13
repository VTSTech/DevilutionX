#include "engine/render/gpu/gpu_d3dcompile.hpp"

#ifdef _WIN32

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <string_view>

#include <windows.h>

// tlhelp32.h needs the windows.h types (HANDLE, DWORD, MAX_PATH); it does not
// include windows.h itself, so keep it below. The blank line keeps clang-format
// from sorting it above windows.h.
#include <tlhelp32.h>

#include "engine/render/gpu/gpu_scaler.hpp"
#include "utils/log.hpp"
#include "utils/str_cat.hpp"

namespace devilution {
namespace gpu_d3dcompile {
namespace {

// Diagnostic logging goes through the GPU scaler's public LogPhase hook so
// every message lands in the same gpu-scaler.log timeline.

void LogFileLine(std::string_view line)
{
	gpu_scaler::LogPhase(line);
}

// --- Guarded D3DCompile ------------------------------------------------------
// A fault inside d3dcompiler (a wild jump into a non-executable page has been
// observed in the wild on D3D12, killing the process before the fallback could
// run) must not take the game down: while the guard is armed on this thread,
// the vectored exception handler below converts the fault into a "compile
// failed" result via longjmp, and the presentation path either retries on the
// Vulkan backend or reverts to the regular renderer for the session. The
// guarded span contains no C++ objects with destructors, so skipping
// unwinding is safe; x64 exception dispatch is table-based, so abandoning the
// faulting frames leaves no stale registration behind.
thread_local bool tCompileGuardArmed = false;
thread_local DWORD tCompileGuardThread = 0;
static jmp_buf tCompileJmpBuf;

// Writes fatal native exceptions (access violations and friends) to the
// diagnostic file before the process dies. Purely diagnostic: the exception
// is always re-raised (EXCEPTION_CONTINUE_SEARCH) so debuggers and WER keep
// working. Guarded exceptions raised internally by drivers or the compiler
// runtime also pass through here; they are counted so a later truly fatal
// crash can still be distinguished.
//
// The handler also records the faulting instruction pointer and a raw stack
// backtrace as offsets from the executable's image base. The executable is
// built with `-g2` (file/line debug info), so these offsets can be resolved
// offline with `addr2line -e devilutionx.exe <offset>` against the exact
// binary that produced the log.
extern "C" USHORT NTAPI RtlCaptureStackBackTrace(
    ULONG FramesToSkip, ULONG FramesToCapture, void **BackTrace, ULONG *BackTraceHash);

std::string HexU64(uint64_t value)
{
	char buf[32];
	std::snprintf(buf, sizeof(buf), "%llX", static_cast<unsigned long long>(value));
	return buf;
}

std::string HexU32(uint32_t value)
{
	return HexU64(value);
}

std::string HexByte(uint8_t value)
{
	char buf[8];
	std::snprintf(buf, sizeof(buf), "%02X", value);
	return buf;
}

std::string CrashThreadName()
{
	using GetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PWSTR *);
	static GetThreadDescriptionFn fn = reinterpret_cast<GetThreadDescriptionFn>(
	    reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
	if (fn != nullptr) {
		PWSTR name = nullptr;
		if (fn(GetCurrentThread(), &name) == S_OK && name != nullptr) {
			std::string result;
			for (const wchar_t *p = name; *p != L'\0' && result.size() < 32; ++p)
				result.push_back(static_cast<char>(*p >= L' ' && *p < L'\x7F' ? *p : L'?'));
			LocalFree(name);
			return result;
		}
	}
	return "";
}

// Minimal COM-ish interface matching the ID3DBlob vtable layout.
struct FakeD3DBlob {
	virtual long STDMETHODCALLTYPE QueryInterface(void *, void **) = 0;
	virtual unsigned long STDMETHODCALLTYPE AddRef() = 0;
	virtual unsigned long STDMETHODCALLTYPE Release() = 0;
	virtual void *STDMETHODCALLTYPE GetBufferPointer() = 0;
	virtual size_t STDMETHODCALLTYPE GetBufferSize() = 0;
};

using D3DCompileFn = long(STDMETHODCALLTYPE *)(
    const void *, size_t, const char *, const void *, void *, const char *,
    const char *, unsigned, unsigned, FakeD3DBlob **, FakeD3DBlob **);

// Lossy ASCII rendering of a Windows path, for the diagnostic log only.
std::string NarrowPath(const wchar_t *wide)
{
	std::string narrow;
	for (const wchar_t *c = wide; *c != L'\0'; ++c)
		narrow.push_back(*c >= 32 && *c < 127 ? static_cast<char>(*c) : '?');
	return narrow;
}

// Resolved d3dcompiler module, kept for fault forensics: the guarded-fault
// report checks whether the faulting instruction pointer is inside it.
HMODULE gD3DCompilerModule = nullptr;

// The resolved D3DCompile entry address, kept for fault forensics: at crash
// time the log compares the faulting instruction pointer against it, so a
// corrupted function pointer (the wild jump called garbage instead of the
// compiler) is distinguishable from a fault deeper inside the compiler.
uintptr_t gD3DCompileProcAddress = 0;

// Exception codes the compile guard contains: the wild-jump access violations
// observed in the field, plus the other immediately fatal code classes, so a
// compiler-side fault can never kill the session. Everything else (C++
// exceptions, breakpoints) passes through untouched.
bool IsFatalForCompile(DWORD code)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_STACK_OVERFLOW:
	case 0xC0000374: /* STATUS_HEAP_CORRUPTION */
	case 0xC0000409: /* STATUS_STACK_BUFFER_OVERRUN */
		return true;
	default:
		return false;
	}
}

// Module-aware address description. The old formatter only knew the exe
// image; a faulting IP that lands in an injected module or in d3dcompiler
// itself was printed as a raw address. Now any module is resolved via
// GetModuleHandleExW(FROM_ADDRESS), so the log names "path.dll+0xOFFSET"
// directly, and a raw address explicitly says it is not in any loaded
// module (the signature of a jump into unmapped or data memory).
std::string DescribeAddress(uintptr_t address)
{
	const HMODULE exeModule = GetModuleHandleW(nullptr);
	const uintptr_t exeBase = reinterpret_cast<uintptr_t>(exeModule);
	uintptr_t exeEnd = exeBase;
	if (exeModule != nullptr) {
		const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(exeModule);
		if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
			const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(exeBase + dosHeader->e_lfanew);
			exeEnd = exeBase + ntHeaders->OptionalHeader.SizeOfImage;
		}
	}
	if (address >= exeBase && address < exeEnd)
		return StrCat("devilutionx.exe+0x", HexU64(static_cast<uint64_t>(address - exeBase)));
	HMODULE containing = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        reinterpret_cast<LPCWSTR>(address), &containing)
	    && containing != nullptr) {
		wchar_t path[MAX_PATH] = {};
		GetModuleFileNameW(containing, path, MAX_PATH);
		const uintptr_t base = reinterpret_cast<uintptr_t>(containing);
		return StrCat(NarrowPath(path), "+0x", HexU64(static_cast<uint64_t>(address - base)));
	}
	return StrCat("0x", HexU64(static_cast<uint64_t>(address)), " (not in any loaded module)");
}

// Case-insensitive ASCII substring test over a wide module name, for the
// known-injector verdicts below.
bool WcsIContains(const wchar_t *haystack, const char *needle)
{
	const auto lowerW = [](wchar_t c) -> char {
		return static_cast<char>(c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c);
	};
	for (const wchar_t *h = haystack; *h != L'\0'; ++h) {
		const char *n = needle;
		for (const wchar_t *s = h; *n != '\0' && *s != L'\0' && lowerW(*s) == *n; ++s, ++n) {
		}
		if (*n == '\0')
			return true;
	}
	return false;
}

// Full module map with base addresses: the faulting instruction pointer is
// resolved against every range (Toolhelp, not GetModuleHandleEx, so a broken
// loader cache cannot hide the containing module), overlay/capture hooks get
// an explicit verdict, and every foreign module keeps its full path so old
// log parsing keeps working.
void LogForeignModules(uintptr_t faultingIp)
{
	const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE)
		return;
	wchar_t winDir[MAX_PATH] = {};
	const UINT winLen = GetWindowsDirectoryW(winDir, MAX_PATH);
	MODULEENTRY32W entry;
	entry.dwSize = sizeof(entry);
	bool ipResolved = false;
	int logged = 0;
	if (Module32FirstW(snapshot, &entry)) {
		LogFileLine("  module map (base..end, [S]=system [F]=foreign):");
		do {
			const uintptr_t base = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
			const uintptr_t end = entry.modBaseSize != 0 ? base + entry.modBaseSize - 1 : base;
			const bool foreign = winLen == 0 || _wcsnicmp(entry.szExePath, winDir, winLen) != 0;
			LogFileLine(StrCat("  [", foreign ? "F" : "S", "] 0x", HexU64(static_cast<uint64_t>(base)), "-0x",
			    HexU64(static_cast<uint64_t>(end)), " ", NarrowPath(entry.szModule)));
			if (foreign)
				LogFileLine(StrCat("    path: ", NarrowPath(entry.szExePath)));
			for (const char *needle :
			    { "graphics-hook", "rtsshooks", "rtss64", "fraps", "gameoverlayrenderer", "reshade", "specialk",
			        "overwolf", "apphelp", "aclayers", "acgenral", "detoured" }) {
				if (WcsIContains(entry.szModule, needle))
					LogFileLine(StrCat("    note: known overlay/capture hook present: ", needle));
			}
			if (faultingIp >= base && faultingIp <= end) {
				ipResolved = true;
				if (faultingIp == base)
					LogFileLine(StrCat("    note: faulting ip EQUALS the base of ", NarrowPath(entry.szModule),
					    " - a module base/handle was called as code"));
				else
					LogFileLine(StrCat("    note: faulting ip is inside ", NarrowPath(entry.szModule), " at +0x",
					    HexU64(static_cast<uint64_t>(faultingIp - base))));
			}
			logged += 1;
			if (logged >= 160) {
				LogFileLine("  (module map truncated at 160 entries - later modules, e.g. driver DLLs, are missing)");
				break;
			}
		} while (Module32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	if (!ipResolved && faultingIp != 0)
		LogFileLine("  note: faulting ip is outside every module range (see the memory verdict above)");
}

// Memory-manager verdict for the faulting target address. VirtualQuery is the
// one authoritative answer to "what is at this address": free, reserved,
// committed-executable image memory, or committed private data.
std::string DescribeMemoryRegion(uintptr_t address)
{
	MEMORY_BASIC_INFORMATION mbi = {};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0)
		return StrCat("  memory at 0x", HexU64(static_cast<uint64_t>(address)), ": VirtualQuery failed");
	const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
	const uintptr_t regionEnd = regionBase + mbi.RegionSize - 1;
	std::string line = StrCat("  memory at 0x", HexU64(static_cast<uint64_t>(address)), ": region 0x",
	    HexU64(static_cast<uint64_t>(regionBase)), "-0x", HexU64(static_cast<uint64_t>(regionEnd)));
	switch (mbi.State) {
	case MEM_COMMIT:
		line += " state=COMMITTED";
		break;
	case MEM_RESERVE:
		line += " state=RESERVED";
		break;
	case MEM_FREE:
		line += " state=FREE";
		break;
	default:
		line += StrCat(" state=0x", HexU32(mbi.State));
		break;
	}
	switch (mbi.Type) {
	case MEM_IMAGE:
		line += " type=IMAGE";
		break;
	case MEM_MAPPED:
		line += " type=MAPPED";
		break;
	case MEM_PRIVATE:
		line += " type=PRIVATE";
		break;
	default:
		break;
	}
	// PAGE_EXECUTE=0x10, PAGE_EXECUTE_READ=0x20, PAGE_EXECUTE_READWRITE=0x40,
	// PAGE_EXECUTE_WRITECOPY=0x80; every other protection is non-executable.
	const DWORD lowProt = mbi.Protect & 0xFF;
	const bool executable = lowProt == PAGE_EXECUTE || lowProt == PAGE_EXECUTE_READ
	    || lowProt == PAGE_EXECUTE_READWRITE || lowProt == PAGE_EXECUTE_WRITECOPY;
	if (mbi.State == MEM_COMMIT)
		line += StrCat(" protect=0x", HexU32(lowProt), executable ? " (executable)" : " (not executable)");
	if (mbi.State == MEM_FREE)
		line += " -> UNMAPPED: a pointer into freed/unloaded memory was called";
	else if (mbi.State == MEM_RESERVE)
		line += " -> RESERVED: a pointer into an uncommitted region was called";
	else if (executable && mbi.Type == MEM_IMAGE)
		line += " -> executable image memory (a manually mapped image if the module map below does not cover it)";
	else if (executable && mbi.Type == MEM_PRIVATE)
		line += " -> PRIVATE EXECUTABLE memory (dynamically generated or injected code)";
	else if (mbi.Type == MEM_IMAGE)
		line += " -> image-backed but non-executable (jump into a data section or a discarded region)";
	else
		line += " -> non-executable data page (heap/static/stack): control flow jumped into data";
	if ((mbi.Protect & PAGE_GUARD) != 0)
		line += " [PAGE_GUARD]";
	return line;
}

// Hex dump of the first bytes at the faulting target, with trampoline/PE
// header detection: MZ => a module handle or image base was called as code;
// E9/E8 => a hook trampoline; CC => int3. Reads only within the committed
// region (protection is per-region, so a clamped read cannot fault).
void LogTargetBytes(uintptr_t address)
{
	MEMORY_BASIC_INFORMATION mbi = {};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT)
		return;
	const DWORD lowProt = mbi.Protect & 0xFF;
	if (lowProt == PAGE_NOACCESS || (mbi.Protect & PAGE_GUARD) != 0)
		return;
	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	const size_t count = std::min<uintptr_t>(16, regionEnd - address);
	const auto *bytes = reinterpret_cast<const uint8_t *>(address);
	std::string line = "  bytes at target:";
	for (size_t i = 0; i < count; ++i)
		line += StrCat(" ", HexByte(bytes[i]));
	if (count >= 2 && bytes[0] == 'M' && bytes[1] == 'Z')
		line += "  (MZ header: a module handle/image base was called as code)";
	else if (count >= 1 && bytes[0] == 0xE9)
		line += "  (JMP rel32: hook trampoline)";
	else if (count >= 1 && bytes[0] == 0xE8)
		line += "  (CALL rel32)";
	else if (count >= 1 && bytes[0] == 0xCC)
		line += "  (int3 breakpoint)";
	LogFileLine(line);
}

// The REAL pre-fault call chain. RtlCaptureStackBackTrace from inside the
// vectored handler only captures the handler's own frames (exception dispatch
// and the handler functions below); the exception CONTEXT still holds the
// faulting thread's registers, and x64 table-based unwinding from there
// recovers the frames that led to the fault. A frame without unwind info
// (the wild target itself: freed code, data, or a manually mapped page)
// falls back to reading the return address at Rsp, which names the code that
// transferred control to it. Exe frames print as devilutionx.exe+0xOFFSET and
// resolve offline with addr2line against the exact binary.
void LogContextCallChain(EXCEPTION_POINTERS *info)
{
	if (info == nullptr || info->ContextRecord == nullptr)
		return;
	CONTEXT ctx = *info->ContextRecord;
	std::string chain = "  call chain from faulting context:";
	bool progressed = true;
	for (int depth = 0; depth < 16 && progressed; ++depth) {
		const ULONG64 pc = ctx.Rip;
		if (pc < 0x10000)
			break;
		chain += ' ';
		chain += DescribeAddress(static_cast<uintptr_t>(pc));
		progressed = false;
		ULONG64 imageBase = 0;
		void *fnEntry = RtlLookupFunctionEntry(pc, &imageBase, nullptr);
		if (fnEntry != nullptr) {
			PVOID handlerData = nullptr;
			ULONG64 establisherFrame = 0;
			RtlVirtualUnwind(0, imageBase, pc, static_cast<RUNTIME_FUNCTION *>(fnEntry), &ctx, &handlerData,
			    &establisherFrame, nullptr);
			progressed = ctx.Rip != pc;
		} else {
			// Leaf frame or code without unwind info: the return address sits at Rsp.
			MEMORY_BASIC_INFORMATION mbi = {};
			if (VirtualQuery(reinterpret_cast<LPCVOID>(ctx.Rsp), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT
			    || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
				break;
			ctx.Rip = *reinterpret_cast<const ULONG64 *>(ctx.Rsp);
			ctx.Rsp += sizeof(ULONG64);
			progressed = ctx.Rip != pc;
		}
	}
	LogFileLine(chain);
}

// --- v3: fault microarchitecture -----------------------------------------
// The v2 field log fixed the SHAPE of the wild transfer: at fault time [Rsp]
// holds a devilutionx return address and no d3dcompiler or ntdll frame sits
// between the wild ip and our code. Two readings remain: (a) devilutionx
// called a corrupted/stale pointer directly, or (b) execution entered the
// real D3DCompile and jumped wild without touching the stack. The probes
// below separate (a) from (b): the register file, the raw stack top, and a
// decode of the indirect call/jmp that ends at the return address, including
// the CURRENT contents of the memory slot that supplied the target.

// Read helper for the decode probes: only touches committed, readable
// memory, so a bad slot cannot turn the forensics itself into a crash.
bool SafeRead(uintptr_t address, void *out, size_t size)
{
	MEMORY_BASIC_INFORMATION mbi = {};
	if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &mbi, sizeof(mbi)) == 0 || mbi.State != MEM_COMMIT
	    || (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
		return false;
	const uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	if (address + size > regionEnd)
		return false;
	std::memcpy(out, reinterpret_cast<const void *>(address), size);
	return true;
}

void LogFaultRegisters(EXCEPTION_POINTERS *info)
{
	const CONTEXT &ctx = *info->ContextRecord;
	LogFileLine(StrCat("  registers: rip=0x", HexU64(ctx.Rip), " rsp=0x", HexU64(ctx.Rsp), " rbp=0x", HexU64(ctx.Rbp),
	    " rax=0x", HexU64(ctx.Rax), " rbx=0x", HexU64(ctx.Rbx), " rcx=0x", HexU64(ctx.Rcx), " rdx=0x", HexU64(ctx.Rdx),
	    " rsi=0x", HexU64(ctx.Rsi), " rdi=0x", HexU64(ctx.Rdi)));
	LogFileLine(StrCat("  registers: r8=0x", HexU64(ctx.R8), " r9=0x", HexU64(ctx.R9), " r10=0x", HexU64(ctx.R10),
	    " r11=0x", HexU64(ctx.R11), " r12=0x", HexU64(ctx.R12), " r13=0x", HexU64(ctx.R13), " r14=0x", HexU64(ctx.R14),
	    " r15=0x", HexU64(ctx.R15)));
	if (info->ExceptionRecord != nullptr && info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
	    && info->ExceptionRecord->NumberParameters >= 2) {
		const uintptr_t target = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
		if (target == 0)
			return;
		const unsigned long long regs[] = { ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rbp, ctx.Rsi, ctx.Rdi, ctx.R8,
			ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15 };
		const char *names[] = { "rax", "rcx", "rdx", "rbx", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12",
			"r13", "r14", "r15" };
		for (size_t i = 0; i < sizeof(regs) / sizeof(regs[0]); ++i) {
			if (regs[i] == target)
				LogFileLine(StrCat("  note: ", names[i],
				    " still holds the faulting target - the wild transfer was \"call ", names[i],
				    "\" through a corrupt/stale pointer (or the register kept the callee pointer)"));
		}
	}
}

void LogRawStackTop(EXCEPTION_POINTERS *info)
{
	const CONTEXT &ctx = *info->ContextRecord;
	uint64_t slots[6] = {};
	if (!SafeRead(static_cast<uintptr_t>(ctx.Rsp), slots, sizeof(slots)))
		return;
	std::string line = "  stack top:";
	for (size_t i = 0; i < 6; ++i)
		line += StrCat(" [+0x", HexU64(static_cast<uint64_t>(i * sizeof(uint64_t))), "]=0x", HexU64(slots[i]));
	LogFileLine(line);
}

// Once per process, verify the resolved D3DCompile entry against the
// on-disk DLL: an inline hook (driver/shim/AV layer) rewrites exactly the
// first bytes of the export. MATCH closes the whole hooked-d3dcompiler
// family; a MISMATCH names the hook outright.
void LogD3DCompilerIntegrity(HMODULE mod, uintptr_t procAddress)
{
	static bool done = false;
	if (done || mod == nullptr || procAddress == 0)
		return;
	done = true;
	const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
	const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return;
	const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
	const uintptr_t rva = procAddress - base;
	const IMAGE_SECTION_HEADER *sec = nullptr;
	const auto *sections = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		const IMAGE_SECTION_HEADER &s = sections[i];
		const uintptr_t span = s.Misc.VirtualSize != 0 ? s.Misc.VirtualSize : 1;
		if (rva >= s.VirtualAddress && rva < s.VirtualAddress + span) {
			sec = &s;
			break;
		}
	}
	if (sec == nullptr) {
		LogFileLine("  d3dcompiler integrity: D3DCompile RVA outside all sections; skipped");
		return;
	}
	uint8_t mem[24] = {};
	uint8_t disk[24] = {};
	const bool haveMem = SafeRead(procAddress, mem, sizeof(mem));
	wchar_t path[MAX_PATH] = {};
	GetModuleFileNameW(mod, path, MAX_PATH);
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
	bool haveDisk = false;
	if (file != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER pos;
		pos.QuadPart = static_cast<LONGLONG>(sec->PointerToRawData + (rva - sec->VirtualAddress));
		haveDisk = SetFilePointer(file, pos.LowPart, &pos.HighPart, FILE_BEGIN) != INVALID_SET_FILE_POINTER;
		DWORD got = 0;
		haveDisk = haveDisk && ReadFile(file, disk, sizeof(disk), &got, nullptr) && got == sizeof(disk);
		CloseHandle(file);
	}
	if (!haveMem || !haveDisk) {
		LogFileLine("  d3dcompiler integrity: read failed; skipped");
		return;
	}
	int diff = -1;
	for (size_t i = 0; i < sizeof(mem); ++i) {
		if (mem[i] != disk[i]) {
			diff = static_cast<int>(i);
			break;
		}
	}
	if (diff < 0) {
		LogFileLine("  d3dcompiler integrity: first 24 bytes of D3DCompile match the on-disk file (no inline hook)");
		return;
	}
	std::string line = StrCat("  d3dcompiler integrity: MISMATCH at byte ", diff, " - in-memory:");
	for (size_t i = 0; i < sizeof(mem); ++i)
		line += StrCat(" ", HexByte(mem[i]));
	line += " on-disk:";
	for (size_t i = 0; i < sizeof(disk); ++i)
		line += StrCat(" ", HexByte(disk[i]));
	LogFileLine(line + "  (an inline hook rewrote the export)");
}

// Once per session, right before the first guarded compile: record the
// state of imageBase-1GiB (the exact wild-ip offset observed in the field)
// and of every non-free region in the 2 GiB window below the image base.
void ProbeAddressSpaceBeforeCompile()
{
	static bool done = false;
	if (done)
		return;
	done = true;
	const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	if (exeBase == 0 || exeBase < 0x80000000ull)
		return;
	MEMORY_BASIC_INFORMATION mbi = {};
	const uintptr_t point = exeBase - 0x40000000ull;
	if (VirtualQuery(reinterpret_cast<LPCVOID>(point), &mbi, sizeof(mbi)) != 0) {
		const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		LogFileLine(StrCat("  probe: image base 0x", HexU64(static_cast<uint64_t>(exeBase)), "; imageBase-1GiB=0x",
		    HexU64(static_cast<uint64_t>(point)), " region 0x", HexU64(static_cast<uint64_t>(regionBase)), "-0x",
		    HexU64(static_cast<uint64_t>(regionBase + mbi.RegionSize - 1)), " state=0x", HexU32(mbi.State), " type=0x",
		    HexU32(mbi.Type), mbi.State == MEM_FREE ? " (FREE before any compile)" : " (in use before any compile)"));
	}
	LogFileLine("  probe: non-free regions in the 2 GiB below the image base:");
	int lines = 0;
	for (uintptr_t addr = exeBase - 0x80000000ull; addr < exeBase && lines < 24;) {
		if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0)
			break;
		const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		const uintptr_t next = regionBase + mbi.RegionSize;
		if (mbi.State != MEM_FREE) {
			lines++;
			LogFileLine(StrCat("    0x", HexU64(static_cast<uint64_t>(regionBase)), "-0x",
			    HexU64(static_cast<uint64_t>(next - 1)), " state=0x", HexU32(mbi.State), " type=0x", HexU32(mbi.Type),
			    " size=0x", HexU64(static_cast<uint64_t>(mbi.RegionSize))));
		}
		if (next <= addr)
			break;
		addr = next;
	}
	if (lines == 0)
		LogFileLine("    (none - the whole window is free)");
}

void LogSlotVerdict(uintptr_t slot, uintptr_t target, const std::string &mnemonic)
{
	LogFileLine(StrCat("  call-site decode: ", mnemonic, " -> slot ", DescribeAddress(slot)));
	uint64_t value = 0;
	if (!SafeRead(slot, &value, sizeof(value))) {
		LogFileLine("    slot not readable");
		return;
	}
	LogFileLine(StrCat("    slot now holds 0x", HexU64(value), " (", DescribeAddress(static_cast<uintptr_t>(value)), ")"));
	if (target != 0 && value == target)
		LogFileLine("    verdict: the slot ITSELF holds the wild target - the pointer variable was corrupt/stale "
		            "when called (reading a); addr2line the slot address for the variable name");
	else if (gD3DCompileProcAddress != 0 && value == gD3DCompileProcAddress)
		LogFileLine("    verdict: the slot holds the valid D3DCompile entry - the real compiler was entered; the "
		            "wild jump happened inside it with an unchanged stack (reading b)");
	else
		LogFileLine("    verdict: slot holds neither the wild target nor D3DCompile - decode the raw bytes above");
}

void LogRegVerdict(uint64_t regValue, const char *regName, uintptr_t target)
{
	LogFileLine(StrCat("  call-site decode: call/jmp ", regName, " -> ", regName, " now holds 0x", HexU64(regValue),
	    " (", DescribeAddress(static_cast<uintptr_t>(regValue)), ")"));
	if (target != 0 && regValue == target)
		LogFileLine(StrCat("    verdict: ", regName, " holds the wild target - call/jmp ", regName,
		    " through a corrupt/stale pointer (reading a)"));
	else if (gD3DCompileProcAddress != 0 && regValue == gD3DCompileProcAddress)
		LogFileLine(StrCat("    verdict: ", regName,
		    " still holds the valid D3DCompile entry - the call entered the real compiler; the wild jump happened "
		    "inside it (reading b)"));
	else
		LogFileLine("    verdict: register was clobbered after the transfer (execution progressed inside the callee)");
}

// Decode the control transfer that ends at the return address stored at
// [Rsp] of the faulting context. pre[8] is the byte at retAddr-1, so an
// instruction of length L starts at pre[9-L] and every disp32 form puts its
// displacement at pre[5..8]. Register-relative slots use the faulting
// context's registers (clobbered if the callee progressed; the raw bytes
// allow re-decode either way). SIB forms are skipped.
void LogCallSiteDecode(EXCEPTION_POINTERS *info)
{
	const CONTEXT &ctx = *info->ContextRecord;
	uintptr_t target = 0;
	if (info->ExceptionRecord != nullptr && info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
	    && info->ExceptionRecord->NumberParameters >= 2)
		target = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
	uint64_t ret = 0;
	if (!SafeRead(static_cast<uintptr_t>(ctx.Rsp), &ret, sizeof(ret)) || ret < 0x10000)
		return;
	const uintptr_t retAddr = static_cast<uintptr_t>(ret);
	uint8_t pre[9] = {};
	if (!SafeRead(retAddr - sizeof(pre), pre, sizeof(pre)))
		return;
	std::string bytes = "  call-site bytes:";
	for (size_t i = 0; i < sizeof(pre); ++i)
		bytes += StrCat(" ", HexByte(pre[i]));
	LogFileLine(bytes + "  (9 bytes ending at return-1)");
	const unsigned long long regs[] = { ctx.Rax, ctx.Rcx, ctx.Rdx, ctx.Rbx, ctx.Rsp, ctx.Rbp, ctx.Rsi, ctx.Rdi,
		ctx.R8, ctx.R9, ctx.R10, ctx.R11, ctx.R12, ctx.R13, ctx.R14, ctx.R15 };
	const char *regNames[] = { "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11",
		"r12", "r13", "r14", "r15" };
	const auto le32 = [&](size_t i) -> int32_t {
		return static_cast<int32_t>(static_cast<uint32_t>(pre[i]) | (static_cast<uint32_t>(pre[i + 1]) << 8)
		    | (static_cast<uint32_t>(pre[i + 2]) << 16) | (static_cast<uint32_t>(pre[i + 3]) << 24));
	};
	if (pre[2] == 0x48 && pre[3] == 0xFF && pre[4] == 0x15) { // REX.W FF /2: call qword [rip+disp32]
		const uintptr_t slot = retAddr + static_cast<int64_t>(le32(5));
		LogSlotVerdict(slot, target, "call qword ptr [rip+disp32] (REX.W)");
	} else if (pre[3] == 0xFF && (pre[4] == 0x15 || pre[4] == 0x25)) { // FF /2, FF /4: call/jmp qword [rip+disp32]
		const uintptr_t slot = retAddr + static_cast<int64_t>(le32(5));
		LogSlotVerdict(slot, target, pre[4] == 0x15 ? "call qword ptr [rip+disp32]" : "jmp qword ptr [rip+disp32]");
	} else if (pre[3] == 0xFF && (pre[4] & 0xF8) == 0x90 && (pre[4] & 7) != 4) { // FF /2 mod=10: call [reg+disp32]
		const int r = pre[4] & 7;
		const uintptr_t slot = static_cast<uintptr_t>(regs[r]) + static_cast<uint64_t>(le32(5));
		LogSlotVerdict(slot, target, StrCat("call qword ptr [", regNames[r], "+disp32]"));
	} else if (pre[4] == 0xE8) { // E8 rel32: direct call
		const uintptr_t callee = retAddr + static_cast<int64_t>(le32(5));
		LogFileLine(StrCat("  call-site decode: direct call rel32 -> ", DescribeAddress(callee)));
		if (target != 0 && callee == target)
			LogFileLine("    verdict: a DIRECT call reached the wild target - image relocations are suspect");
	} else if (pre[6] == 0xFF && (pre[7] & 0xF8) == 0x50 && (pre[7] & 7) != 4) { // FF /2 mod=01: call [reg+disp8]
		const int r = pre[7] & 7;
		const uintptr_t slot = static_cast<uintptr_t>(regs[r]) + static_cast<uint64_t>(static_cast<int8_t>(pre[8]));
		LogSlotVerdict(slot, target, StrCat("call qword ptr [", regNames[r], "+disp8]"));
	} else if (pre[6] == 0x41 && pre[7] == 0xFF && ((pre[8] & 0xF8) == 0xD0 || (pre[8] & 0xF8) == 0xE0)) {
		// REX.B FF /2 or FF /4: call/jmp r8-r15
		LogRegVerdict(regs[(pre[8] & 7) + 8], regNames[(pre[8] & 7) + 8], target);
	} else if (pre[7] == 0xFF && ((pre[8] & 0xF8) == 0xD0 || (pre[8] & 0xF8) == 0xE0)) { // FF /2, /4: call/jmp reg
		LogRegVerdict(regs[pre[8] & 7], regNames[pre[8] & 7], target);
	} else {
		LogFileLine(
		    "  call-site decode: no recognizable indirect transfer ends at this return address ([rsp] may hold a stale value)");
	}
}

// Full forensics for one native exception. Called in two situations:
//  - a compile-guard fault (context non-empty): always reported, BEFORE the
//    guard longjmps back and the session continues on another backend. This
//    is the case that used to log a single line with no detail.
//  - any other fatal exception passing through (context empty): reported for
//    the well-known fatal codes only, before the process dies.
// Capped so a crash loop cannot flood the log. The faulting instruction
// pointer and the raw stack are logged module-relative; the build keeps -g2
// debug info, so exe offsets resolve offline with
// `addr2line -e devilutionx.exe <offset>` against the exact binary.
void LogCrashDetails(std::string_view context, EXCEPTION_POINTERS *info)
{
	static int logged = 0;
	if (info == nullptr || info->ExceptionRecord == nullptr || logged >= 8)
		return;
	const DWORD code = info->ExceptionRecord->ExceptionCode;
	const char *what = nullptr;
	switch (code) {
	case 0xC0000005: what = "access violation"; break;
	case 0xC000001D: what = "illegal instruction"; break;
	case 0xC0000094: what = "integer divide by zero"; break;
	case 0xC00000FD: what = "stack overflow"; break;
	case 0xC0000374: what = "heap corruption"; break;
	case 0xC0000409: what = "stack buffer overrun"; break;
	default: break;
	}
	if (context.empty() && what == nullptr)
		return;
	logged += 1;
	const std::string threadName = CrashThreadName();
	std::string crashLine = StrCat(
	    "CRASH #", logged, ": ", what != nullptr ? what : "exception",
	    " (code=0x", HexU32(static_cast<uint32_t>(code)), ", ");
	if (code == 0xC0000005 && info->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR kind = info->ExceptionRecord->ExceptionInformation[0];
		const char *access = kind == 0 ? "read from" : kind == 1 ? "write to"
		                                                         : "execute of";
		crashLine += StrCat(access, " address 0x",
		    HexU64(static_cast<uint64_t>(info->ExceptionRecord->ExceptionInformation[1])), ", ");
	}
	crashLine += StrCat("thread ", GetCurrentThreadId());
	if (!threadName.empty())
		crashLine += StrCat(" \"", threadName, "\"");
	crashLine += ')';
	if (!context.empty())
		crashLine += StrCat(" [", context, "]");
	LogFileLine(crashLine);

	uintptr_t faultingIp = 0;
	if (info->ContextRecord != nullptr) {
		faultingIp = reinterpret_cast<uintptr_t>(info->ContextRecord->Rip);
		LogFileLine(StrCat("  faulting ip: ", DescribeAddress(faultingIp)));
		// What lives at the target address, and what its first bytes say: this
		// single verdict separates "jumped into unmapped/freed memory" from
		// "jumped into data" from "manually mapped code page".
		LogFileLine(DescribeMemoryRegion(faultingIp));
		LogTargetBytes(faultingIp);
		const uintptr_t exeBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
		if (exeBase != 0 && faultingIp < exeBase)
			LogFileLine(StrCat("  note: faulting ip is 0x", HexU64(static_cast<uint64_t>(exeBase - faultingIp)),
			    " BELOW the image base (0x", HexU64(static_cast<uint64_t>(exeBase)), ")"));
		else if (exeBase != 0)
			LogFileLine(StrCat("  note: faulting ip is 0x", HexU64(static_cast<uint64_t>(faultingIp - exeBase)),
			    " ABOVE the image base (0x", HexU64(static_cast<uint64_t>(exeBase)), ")"));
		if (gD3DCompileProcAddress != 0) {
			LogFileLine(StrCat("  pD3DCompile: 0x", HexU64(static_cast<uint64_t>(gD3DCompileProcAddress)), " (",
			    DescribeAddress(gD3DCompileProcAddress), ")"));
			if (faultingIp == gD3DCompileProcAddress)
				LogFileLine("  note: the faulting ip IS the D3DCompile entry address (the function pointer was already corrupt when called)");
		}
		// The real pre-fault call chain (the backtrace below only shows the
		// handler's own frames).
		LogContextCallChain(info);
		LogFaultRegisters(info);
		LogRawStackTop(info);
		LogCallSiteDecode(info);
	}
	// The explicit verdict users ask for first: did we die inside the system
	// HLSL compiler itself?
	if (gD3DCompilerModule != nullptr && info->ContextRecord != nullptr) {
		HMODULE at = nullptr;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		        reinterpret_cast<LPCWSTR>(info->ContextRecord->Rip), &at)
		    && at == gD3DCompilerModule)
			LogFileLine("  note: the faulting ip is inside d3dcompiler itself");
	}
	void *frames[24] = {};
	const USHORT numFrames = RtlCaptureStackBackTrace(0, 24, frames, nullptr);
	std::string stack = "  handler stack:";
	for (USHORT i = 0; i < numFrames; ++i) {
		stack += ' ';
		stack += DescribeAddress(reinterpret_cast<uintptr_t>(frames[i]));
	}
	LogFileLine(stack);
	static int moduleMapsDumped = 0;
	if (moduleMapsDumped < 2) {
		moduleMapsDumped += 1;
		LogForeignModules(faultingIp);
	}
}

long NTAPI ScalerCrashHandler(EXCEPTION_POINTERS *info)
{
	if (tCompileGuardArmed && info != nullptr && info->ExceptionRecord != nullptr
	    && IsFatalForCompile(info->ExceptionRecord->ExceptionCode)
	    && GetCurrentThreadId() == tCompileGuardThread) {
		tCompileGuardArmed = false;
		LogFileLine("ERROR: fault inside D3DCompile; recovered by the compile guard");
		LogCrashDetails("contained by the compile guard", info);
		longjmp(tCompileJmpBuf, 1);
	}
	LogCrashDetails({}, info);
	return EXCEPTION_CONTINUE_SEARCH;
}

D3DCompileFn LoadD3DCompile()
{
	static D3DCompileFn fn = nullptr;
	if (fn != nullptr)
		return fn;
	// Load by explicit path from the system directory. The default search
	// order would pick up a same-named d3dcompiler DLL sitting in the game
	// folder first, and stale copies shipped by old games are the leading
	// suspect for wild jumps inside D3DCompile.
	wchar_t sysDir[MAX_PATH];
	const UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
	if (sysLen == 0 || sysLen >= MAX_PATH - 32) {
		LogError("GPU scaler: could not resolve the system directory");
		return nullptr;
	}
	HMODULE mod = nullptr;
	for (const wchar_t *name : { L"\\d3dcompiler_47.dll", L"\\d3dcompiler_43.dll" }) {
		std::wstring path(sysDir);
		path += name;
		mod = LoadLibraryW(path.c_str());
		if (mod != nullptr)
			break;
	}
	if (mod == nullptr)
		return nullptr;
	// Log exactly which d3dcompiler DLL ended up in the process: Windows
	// matches LoadLibrary by base file name, so if any component loaded a
	// same-named DLL from the game folder earlier, the full system path above
	// still resolves to that (possibly broken) module.
	wchar_t modPath[MAX_PATH] = {};
	GetModuleFileNameW(mod, modPath, MAX_PATH);
	const std::string loaded = NarrowPath(modPath);
	const std::string systemDir = NarrowPath(sysDir);
	if (loaded.rfind(systemDir, 0) == 0) {
		LogFileLine(StrCat("d3dcompiler: ", loaded));
	} else {
		LogFileLine(StrCat("WARNING: d3dcompiler loaded from outside the system directory: ", loaded,
		    " (a same-named DLL next to the game was loaded earlier by name; delete the local copy)"));
	}
	gD3DCompilerModule = mod;
	fn = reinterpret_cast<D3DCompileFn>(reinterpret_cast<void *>(GetProcAddress(mod, "D3DCompile")));
	if (fn == nullptr) {
		LogError("GPU scaler: found d3dcompiler dll but no D3DCompile export");
		return nullptr;
	}
	// Provenance of the function pointer: log the entry address together
	// with the loaded image range, and warn if GetProcAddress returned an
	// address outside it (a corrupted loader/heap state signature).
	gD3DCompileProcAddress = reinterpret_cast<uintptr_t>(fn);
	uintptr_t modEnd = reinterpret_cast<uintptr_t>(mod);
	const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(mod);
	if (dosHeader->e_magic == IMAGE_DOS_SIGNATURE) {
		const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(
		    reinterpret_cast<uintptr_t>(mod) + dosHeader->e_lfanew);
		modEnd += ntHeaders->OptionalHeader.SizeOfImage;
	}
	LogFileLine(StrCat("d3dcompiler: pD3DCompile=0x", HexU64(static_cast<uint64_t>(gD3DCompileProcAddress)),
	    " module=0x", HexU64(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mod))), "-0x",
	    HexU64(static_cast<uint64_t>(modEnd))));
	if (gD3DCompileProcAddress < reinterpret_cast<uintptr_t>(mod) || gD3DCompileProcAddress >= modEnd)
		LogFileLine("WARNING: D3DCompile export resolved outside the d3dcompiler image range");
	LogD3DCompilerIntegrity(mod, gD3DCompileProcAddress);
	return fn;
}

// Bare invocation wrapper for D3DCompile: must not contain C++ objects with
// destructors, because a fault mid-call resumes here via longjmp from the
// vectored exception handler.
long InvokeD3DCompile(D3DCompileFn d3dCompile, const char *source, const char *target,
    FakeD3DBlob **code, FakeD3DBlob **errors)
{
	volatile long hr = static_cast<long>(0x8000FFFFu); // E_UNEXPECTED
	if (setjmp(tCompileJmpBuf) != 0) {
		// Faulted inside d3dcompiler; the handler has disarmed the guard.
		return hr;
	}
	tCompileGuardArmed = true;
	tCompileGuardThread = GetCurrentThreadId();
	hr = d3dCompile(source, std::strlen(source), nullptr, nullptr, nullptr,
	    "main", target, 0, 0, code, errors);
	tCompileGuardArmed = false;
	return hr;
}

// The same containment for the returned blob objects as for the D3DCompile
// call itself: a broken d3dcompiler can hand back a blob whose vtable is
// garbage, and the wild jumps observed in the wild happened on these virtual
// calls right after D3DCompile returned, not inside it.
const void *GuardedBlobPointer(FakeD3DBlob *blob)
{
	const volatile void *result = nullptr;
	if (setjmp(tCompileJmpBuf) != 0)
		return nullptr;
	tCompileGuardArmed = true;
	tCompileGuardThread = GetCurrentThreadId();
	result = blob->GetBufferPointer();
	tCompileGuardArmed = false;
	return const_cast<const void *>(result);
}

size_t GuardedBlobSize(FakeD3DBlob *blob)
{
	volatile size_t result = 0;
	if (setjmp(tCompileJmpBuf) != 0)
		return 0;
	tCompileGuardArmed = true;
	tCompileGuardThread = GetCurrentThreadId();
	result = blob->GetBufferSize();
	tCompileGuardArmed = false;
	return result;
}

void GuardedBlobRelease(FakeD3DBlob *blob)
{
	if (blob == nullptr)
		return;
	if (setjmp(tCompileJmpBuf) != 0)
		return;
	tCompileGuardArmed = true;
	tCompileGuardThread = GetCurrentThreadId();
	blob->Release();
	tCompileGuardArmed = false;
}

std::vector<uint8_t> CompileHLSL(const char *source, const char *target)
{
	std::vector<uint8_t> blob;
	D3DCompileFn d3dCompile = LoadD3DCompile();
	if (d3dCompile == nullptr) {
		LogError("GPU scaler: d3dcompiler not found, cannot compile shaders for D3D12");
		return blob;
	}
	ProbeAddressSpaceBeforeCompile();
	FakeD3DBlob *code = nullptr;
	FakeD3DBlob *errors = nullptr;
	const long hr = InvokeD3DCompile(d3dCompile, source, target, &code, &errors);
	if (hr == static_cast<long>(0x8000FFFFu)) {
		// The compile guard caught a fault inside d3dcompiler.
		LogError("GPU scaler: D3DCompile faulted and was contained; treating '{}' as a compile failure", target);
		LogFileLine(StrCat("ERROR: D3DCompile faulted (", target, "); contained by the compile guard"));
		return blob;
	}
	if (hr < 0 || code == nullptr) {
		std::string msg;
		if (errors != nullptr) {
			const char *errorBytes = static_cast<const char *>(GuardedBlobPointer(errors));
			const size_t errorSize = GuardedBlobSize(errors);
			GuardedBlobRelease(errors);
			if (errorBytes != nullptr && errorSize > 0)
				msg.assign(errorBytes, std::min<size_t>(errorSize, 400));
		}
		for (char &c : msg) {
			if (c == '\n' || c == '\r')
				c = ' ';
		}
		LogError("GPU scaler: HLSL compilation failed ({}): {}", target, msg);
		// Also record it in the diagnostic file: SDL_Log is invisible
		// for GUI applications on Windows.
		LogFileLine(StrCat("ERROR: HLSL compilation failed (", target, "): ", msg));
		return blob;
	}
	const auto *bytes = static_cast<const uint8_t *>(GuardedBlobPointer(code));
	const size_t size = GuardedBlobSize(code);
	GuardedBlobRelease(code);
	if (bytes == nullptr || size == 0) {
		LogFileLine(StrCat("ERROR: d3dcompiler returned an unusable shader blob for '", target, "'"));
		return blob;
	}
	blob.assign(bytes, bytes + size);
	return blob;
}

} // namespace

void PreloadSystemCompiler()
{
	wchar_t sysDir[MAX_PATH];
	const UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
	if (sysLen == 0 || sysLen >= MAX_PATH - 32)
		return;
	for (const wchar_t *name : { L"\\d3dcompiler_47.dll", L"\\d3dcompiler_43.dll" }) {
		std::wstring path(sysDir);
		path += name;
		if (LoadLibraryW(path.c_str()) != nullptr)
			return; // Keep it loaded for the whole process.
	}
}

void InstallCrashGuard()
{
	static bool installed = false;
	if (!installed) {
		installed = true;
		AddVectoredExceptionHandler(/*First=*/1, &ScalerCrashHandler);
	}
}

std::vector<uint8_t> Compile(const char *source, const char *target)
{
	return CompileHLSL(source, target);
}

} // namespace gpu_d3dcompile
} // namespace devilution

#endif // _WIN32
