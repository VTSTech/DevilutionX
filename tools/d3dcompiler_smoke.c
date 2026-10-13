// tools/d3dcompiler_smoke.c
//
// Standalone smoke test for the D3D12 shader-compiler path used by the
// DevilutionX GPU scaler. No game code, no SDL: loads the system
// d3dcompiler by ABSOLUTE path (same policy as the game), compiles the
// exact vertex shader the scaler compiles first plus a tiny and a heavy
// realistic fragment shader, and prints result, timing and fault
// forensics for each stage.
//
// It answers one question: does d3dcompiler work in a plain process on
// this machine?
//  - The tool itself faults  -> machine-level problem (broken/patched
//    system d3dcompiler_47.dll, forced Exploit Protection/ACG, security
//    software). Fix the machine: `sfc /scannow`, Exploit Protection
//    defaults, a clean boot.
//  - The tool passes, the game faults -> process-context problem
//    (injected DLLs). The game's gpu-scaler.log now names the module
//    (see the "non-system module:" and "faulting ip:" lines).
//
// Output goes to the console and to d3dcompiler-smoke.log next to the
// executable. Exit code: 0 all compiles OK, 1 any failure/fault,
// 2 d3dcompiler not loadable.
//
// Build (MinGW-w64): x86_64-w64-mingw32-gcc -O2 -o d3dcompiler_smoke.exe d3dcompiler_smoke.c -lversion
// Build (MSVC):      cl /O2 d3dcompiler_smoke.c version.lib
// Run:               d3dcompiler_smoke.exe

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static HANDLE gLogFile = INVALID_HANDLE_VALUE;

static void Emit(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = '\0';
	fputs(buf, stdout);
	fputc('\n', stdout);
	fflush(stdout);
	if (gLogFile == INVALID_HANDLE_VALUE) {
		char path[MAX_PATH];
		const DWORD n = GetModuleFileNameA(NULL, path, MAX_PATH);
		char *slash = n ? strrchr(path, '\\') : NULL;
		if (slash != NULL)
			*(slash + 1) = '\0';
		else
			path[0] = '\0';
		strcat(path, "d3dcompiler-smoke.log");
		gLogFile = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
		    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	}
	if (gLogFile != INVALID_HANDLE_VALUE) {
		DWORD written = 0;
		WriteFile(gLogFile, buf, (DWORD)strlen(buf), &written, NULL);
		WriteFile(gLogFile, "\r\n", 2, &written, NULL);
		FlushFileBuffers(gLogFile);
	}
}

static char *Narrow(const wchar_t *wide)
{
	static char buf[MAX_PATH * 2];
	char *out = buf;
	for (const wchar_t *c = wide; *c != L'\0' && out < buf + sizeof(buf) - 1; ++c)
		*out++ = (*c >= 32 && *c < 127) ? (char)*c : '?';
	*out = '\0';
	return buf;
}

// --- fault forensics (same data the game logs) ------------------------------

static const char *DescribeAddress(unsigned long long address, char *out, size_t outSize)
{
	const HMODULE exe = GetModuleHandleW(NULL);
	const unsigned long long exeBase = (unsigned long long)(uintptr_t)exe;
	unsigned long long exeEnd = exeBase;
	if (exe != NULL) {
		const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)exeBase;
		if (dos->e_magic == IMAGE_DOS_SIGNATURE)
			exeEnd = exeBase + ((const IMAGE_NT_HEADERS *)(exeBase + dos->e_lfanew))->OptionalHeader.SizeOfImage;
	}
	if (address >= exeBase && address < exeEnd) {
		_snprintf(out, outSize, "devilutionx_smoke.exe+0x%llX", address - exeBase);
		return out;
	}
	HMODULE containing = NULL;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	        (LPCWSTR)address, &containing)
	    && containing != NULL) {
		wchar_t wpath[MAX_PATH] = L"";
		GetModuleFileNameW(containing, wpath, MAX_PATH);
		_snprintf(out, outSize, "%s+0x%llX", Narrow(wpath), address - (unsigned long long)(uintptr_t)containing);
		return out;
	}
	_snprintf(out, outSize, "0x%llX (not in any loaded module)", address);
	return out;
}

static void LogForeignModules(void)
{
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	if (snap == INVALID_HANDLE_VALUE)
		return;
	wchar_t winDir[MAX_PATH] = L"";
	const UINT winLen = GetWindowsDirectoryW(winDir, MAX_PATH);
	MODULEENTRY32W entry;
	entry.dwSize = sizeof(entry);
	int logged = 0;
	if (Module32FirstW(snap, &entry)) {
		do {
			if (winLen == 0 || _wcsnicmp(entry.szExePath, winDir, winLen) != 0) {
				Emit("  non-system module: %s", Narrow(entry.szExePath));
				if (++logged >= 12) {
					Emit("  (module list truncated)");
					break;
				}
			}
		} while (Module32NextW(snap, &entry));
	}
	CloseHandle(snap);
}

static jmp_buf gJmp;
static volatile long gArmed = 0;
static volatile DWORD gArmedThread = 0;

static long NTAPI CrashHandler(EXCEPTION_POINTERS *info)
{
	if (gArmed && info != NULL && info->ExceptionRecord != NULL
	    && (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
	           || info->ExceptionRecord->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION
	           || info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW
	           || info->ExceptionRecord->ExceptionCode == 0xC0000374
	           || info->ExceptionRecord->ExceptionCode == 0xC0000409)
	    && GetCurrentThreadId() == gArmedThread) {
		gArmed = 0;
		Emit("FAULT inside D3DCompile (contained)");
		const DWORD code = info->ExceptionRecord->ExceptionCode;
		Emit("  exception code: 0x%08lX", (unsigned long)code);
		if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
			const char *kind = info->ExceptionRecord->ExceptionInformation[0] == 0   ? "read from"
			    : info->ExceptionRecord->ExceptionInformation[0] == 1                ? "write to"
			    : info->ExceptionRecord->ExceptionInformation[0] == 8                ? "execute of"
			    : "access";
			char where[256];
			Emit("  %s %s", kind,
			    DescribeAddress((unsigned long long)info->ExceptionRecord->ExceptionInformation[1], where, sizeof(where)));
		}
		if (info->ContextRecord != NULL) {
			char where[512];
			Emit("  faulting ip: %s",
			    DescribeAddress((unsigned long long)info->ContextRecord->Rip, where, sizeof(where)));
		}
		void *frames[24] = { 0 };
		const USHORT n = RtlCaptureStackBackTrace(0, 24, frames, NULL);
		for (USHORT i = 0; i < n; ++i) {
			char where[512];
			Emit("  stack: %s", DescribeAddress((unsigned long long)(uintptr_t)frames[i], where, sizeof(where)));
		}
		LogForeignModules();
		longjmp(gJmp, 1);
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

// --- guarded D3DCompile ------------------------------------------------------

/* ID3DBlob is a COM object: the pointer we get back from D3DCompile points
 * at an instance whose first member is the vtable pointer. Plain C code
 * walks that vtable by hand; no headers from the D3D SDK are needed. */
typedef struct BlobVtbl {
	long(STDMETHODCALLTYPE *QueryInterface)(void *, void **, void **);
	unsigned long(STDMETHODCALLTYPE *AddRef)(void *);
	unsigned long(STDMETHODCALLTYPE *Release)(void *);
	void *(STDMETHODCALLTYPE *GetBufferPointer)(void *);
	SIZE_T(STDMETHODCALLTYPE *GetBufferSize)(void *);
} BlobVtbl;

typedef struct Blob {
	BlobVtbl *lpVtbl;
} Blob;

typedef long(STDMETHODCALLTYPE *D3DCompileFn)(const void *, SIZE_T, const char *, const void *, void *,
    const char *, const char *, UINT, UINT, void **, void **);

static D3DCompileFn gCompile = NULL;

static long GuardedCompile(const char *source, const char *target, void **code, void **errors)
{
	volatile long hr = (long)0x8000FFFF; /* E_UNEXPECTED: set by the handler on fault */
	if (setjmp(gJmp) != 0)
		return hr;
	gArmed = 1;
	gArmedThread = GetCurrentThreadId();
	hr = gCompile(source, strlen(source), NULL, NULL, NULL, "main", target, 0, 0, code, errors);
	gArmed = 0;
	return hr;
}

// --- shader sources ----------------------------------------------------------
// kVertex and kHqxIndex are extracted verbatim from
// Source/engine/render/gpu/gpu_shaders_hlsl.hpp; the vertex shader is the
// exact source the scaler compiles first on D3D12.

static const char *kVertex =
R"HLSL(struct VSOut
{
        float4 pos : SV_Position;
        float2 uv : TEXCOORD0;
};

VSOut main(uint vid : SV_VertexID)
{
        VSOut o;
        // Fullscreen triangle: (-1,-1), (3,-1), (-1,3) - both axes overscan to 3.
        float2 pos = float2(float((vid & 1u) << 2) - 1.0, float((vid & 2u) << 1) - 1.0);
        o.uv = (pos + 1.0) * 0.5;
        o.pos = float4(pos, 0.0, 1.0);
        return o;
})HLSL";

static const char *kHqxFragment =
R"HLSL(Texture2D uTex : register(t0, space2);
SamplerState uSamp : register(s0, space2);
cbuffer FragUniform : register(b0, space3)
{
        float4 uSourceSize;
        float4 uParams;
};

static const float3 yuvThreshold = float3(48.0 / 255.0, 7.0 / 255.0, 6.0 / 255.0);
static const float3x3 yuv = float3x3(
    float3(0.299, 0.587, 0.114),
    float3(-0.169, -0.331, 0.5),
    float3(0.5, -0.419, -0.081));
static const float3 yuvOffset = float3(0.0, 0.5, 0.5);

bool diff(float3 a, float3 b)
{
        return any(abs((a + yuvOffset) - (b + yuvOffset)) > yuvThreshold);
}

float4 main(float2 vUV : TEXCOORD0) : SV_Target
{
        float dx = uSourceSize.z;
        float dy = uSourceSize.w;

        float3 w1 = mul(yuv, uTex.Sample(uSamp, vUV + float2(-dx, -dy)).rgb);
        float3 w2 = mul(yuv, uTex.Sample(uSamp, vUV + float2(0.0, -dy)).rgb);
        float3 w3 = mul(yuv, uTex.Sample(uSamp, vUV + float2(dx, -dy)).rgb);
        float3 w4 = mul(yuv, uTex.Sample(uSamp, vUV + float2(-dx, 0.0)).rgb);
        float3 w5 = mul(yuv, uTex.Sample(uSamp, vUV).rgb);
        float3 w6 = mul(yuv, uTex.Sample(uSamp, vUV + float2(dx, 0.0)).rgb);
        float3 w7 = mul(yuv, uTex.Sample(uSamp, vUV + float2(-dx, dy)).rgb);
        float3 w8 = mul(yuv, uTex.Sample(uSamp, vUV + float2(0.0, dy)).rgb);
        float3 w9 = mul(yuv, uTex.Sample(uSamp, vUV + float2(dx, dy)).rgb);

        float3 pattern_1 = float3(diff(w5, w1), diff(w5, w2), diff(w5, w3));
        float3 pattern_2 = float3(diff(w5, w4), false, diff(w5, w6));
        float3 pattern_3 = float3(diff(w5, w7), diff(w5, w8), diff(w5, w9));
        float4 crossPat = float4(diff(w4, w2), diff(w2, w6), diff(w8, w4), diff(w6, w8));

        float indexX = dot(pattern_1, float3(1.0, 2.0, 4.0))
            + dot(pattern_2, float3(8.0, 0.0, 16.0))
            + dot(pattern_3, float3(32.0, 64.0, 128.0));
        float indexY = dot(crossPat, float4(1.0, 2.0, 4.0, 8.0));

        return float4(indexX / 255.0, indexY / 15.0, 0.0, 1.0);
})HLSL";

static const char *kTinyFragment =
R"HLSL(Texture2D uTex : register(t0, space2);
SamplerState uSamp : register(s0, space2);
cbuffer FragUniform : register(b0, space3)
{
	float4 uSourceSize;
	float4 uParams;
};

float4 main(float2 vUV : TEXCOORD0) : SV_Target
{
	return uTex.SampleLevel(uSamp, vUV, 0.0);
})HLSL";

// --- d3dcompiler identity ----------------------------------------------------

static void LogCompilerIdentity(const wchar_t *resolvedPath)
{
	Emit("d3dcompiler: %s", Narrow(resolvedPath));
	DWORD handle = 0;
	const DWORD size = GetFileVersionInfoSizeW(resolvedPath, &handle);
	if (size > 0 && size <= 4096) {
		static unsigned char block[4096];
		if (GetFileVersionInfoW(resolvedPath, 0, size, block)) {
			VS_FIXEDFILEINFO *ffi = NULL;
			UINT len = 0;
			if (VerQueryValueW(block, L"\\", (void **)&ffi, &len) && ffi != NULL) {
				Emit("  version: %u.%u.%u.%u",
				    HIWORD(ffi->dwFileVersionMS), LOWORD(ffi->dwFileVersionMS),
				    HIWORD(ffi->dwFileVersionLS), LOWORD(ffi->dwFileVersionLS));
			}
		}
	}
}

int main(void)
{
	AddVectoredExceptionHandler(1, &CrashHandler);

	wchar_t sysDir[MAX_PATH];
	const UINT sysLen = GetSystemDirectoryW(sysDir, MAX_PATH);
	if (sysLen == 0 || sysLen >= MAX_PATH - 32) {
		Emit("FATAL: cannot resolve the system directory");
		return 2;
	}

	HMODULE mod = NULL;
	wchar_t modPath[MAX_PATH] = L"";
	{
		const wchar_t *names[] = { L"\\d3dcompiler_47.dll", L"\\d3dcompiler_43.dll" };
		for (int i = 0; i < 2 && mod == NULL; ++i) {
			wchar_t path[MAX_PATH];
			wcscpy(path, sysDir);
			wcscat(path, names[i]);
			mod = LoadLibraryW(path);
			if (mod != NULL)
				GetModuleFileNameW(mod, modPath, MAX_PATH);
		}
	}
	if (mod == NULL) {
		Emit("FATAL: no system d3dcompiler found (tried 47 and 43)");
		return 2;
	}
	LogCompilerIdentity(modPath);
	if (_wcsnicmp(modPath, sysDir, sysLen) != 0)
		Emit("WARNING: resolved module is OUTSIDE the system directory (a same-named DLL was already loaded)");

	gCompile = (D3DCompileFn)(void *)GetProcAddress(mod, "D3DCompile");
	if (gCompile == NULL) {
		Emit("FATAL: d3dcompiler has no D3DCompile export");
		return 2;
	}

	Emit("loaded modules outside the Windows directory:");
	LogForeignModules();

	struct Stage {
		const char *label;
		const char *source;
		const char *targets[2];
	} stages[] = {
		{ "vertex (scaler kVertex)", kVertex, { "vs_5_1", "vs_5_0" } },
		{ "fragment (tiny sampler)", kTinyFragment, { "ps_5_1", "ps_5_0" } },
		{ "fragment (hqx-style, heavy)", kHqxFragment, { "ps_5_1", "ps_5_0" } },
	};

	int failures = 0;
	for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i) {
		Emit("stage %zu: %s", i + 1, stages[i].label);
		long hr = (long)0x8000FFFF;
		void *code = NULL;
		void *errors = NULL;
		const char *usedTarget = NULL;
		LARGE_INTEGER t0, t1, freq;
		QueryPerformanceFrequency(&freq);
		QueryPerformanceCounter(&t0);
		for (int t = 0; t < 2 && usedTarget == NULL; ++t) {
			code = NULL;
			errors = NULL;
			hr = GuardedCompile(stages[i].source, stages[i].targets[t], &code, &errors);
			if (hr >= 0 && code != NULL)
				usedTarget = stages[i].targets[t];
			else if (t == 0)
				Emit("  %s failed (hr=0x%08lX), retrying with %s", stages[i].targets[t],
				    (unsigned long)hr, stages[i].targets[1]);
		}
		QueryPerformanceCounter(&t1);
		const double ms = usedTarget != NULL ? (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)freq.QuadPart : 0.0;
		if (usedTarget != NULL) {
			BlobVtbl *vt = ((Blob *)code)->lpVtbl;
			Emit("  PASS: %s, %u bytes, %.1f ms", usedTarget,
			    (unsigned)vt->GetBufferSize(code), ms);
			vt->Release(code);
		} else if (hr == (long)0x8000FFFF) {
			Emit("  FAULT: D3DCompile faulted at both targets (contained by the guard)");
			failures++;
		} else {
			char msg[512] = "";
			if (errors != NULL) {
				BlobVtbl *vt = ((Blob *)errors)->lpVtbl;
				const char *bytes = (const char *)vt->GetBufferPointer(errors);
				SIZE_T size = vt->GetBufferSize(errors);
				SIZE_T n = size < sizeof(msg) - 1 ? size : sizeof(msg) - 1;
				memcpy(msg, bytes, n);
				msg[n] = '\0';
				for (char *c = msg; *c; ++c)
					if (*c == '\n' || *c == '\r')
						*c = ' ';
				vt->Release(errors);
			}
			Emit("  FAIL: hr=0x%08lX compiler said: %s", (unsigned long)hr, msg);
			failures++;
		}
	}

	if (failures == 0)
		Emit("RESULT: all stages compiled OK - d3dcompiler works in a plain process on this machine");
	else
		Emit("RESULT: %d stage(s) failed - d3dcompiler is broken at machine level (see the checklist)", failures);
	if (gLogFile != INVALID_HANDLE_VALUE)
		CloseHandle(gLogFile);
	return failures == 0 ? 0 : 1;
}
