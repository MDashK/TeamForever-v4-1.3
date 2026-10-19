/*
*	Xbox 360 port of RSDKv4 (Sonic 1 Forever / Sonic 2 Absolute).
*
*	Process-level platform glue, without touching the engine's main():
*	 - X360_WriteLog (the engine's PrintLog on the 360): lines go to game:\rsdkv4.log, written by a background
*	   thread because writes to the console's storage are slow (especially USB), and to the kernel debug output
*	   (DbgPrint: xenia.log, or a devkit's debugger)
*	 - unhandled exceptions are logged with a stack back-trace (symbolize with x360/tools/addr2sym.py and the
*	   default.elf next to the XEX in the build output)
*	 - when the game exits, go back to the dashboard: with the RXDK runtime, returning from main would reboot
*	   the console
*	All of it is set up by a static constructor, i.e. before main() runs.
*/

#include <xtl.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

extern "C" ULONG DbgPrint(const char* format, ...);

#if defined(S1F_BUILD)
	#define X360_GAME_NAME "Sonic 1 Forever"
#elif defined(S2A_BUILD)
	#define X360_GAME_NAME "Sonic 2 Absolute"
#else
	#define X360_GAME_NAME "RSDKv4"
#endif


namespace
{
	class AsyncLog
	{
	public:
		// The log never grows beyond this (a script printing every frame would otherwise fill the drive)
		static const size_t MAX_FILE_BYTES = 8 * 1024 * 1024;

		explicit AsyncLog(const char* filename)
		{
			InitializeCriticalSection(&mBufferLock);
			InitializeCriticalSection(&mFileLock);
			mFile = CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			mWakeUpEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			mStartTicks = GetTickCount();
			HANDLE thread = CreateThread(nullptr, 64 * 1024, &AsyncLog::writerThreadFunction, this, CREATE_SUSPENDED, nullptr);
			if (nullptr != thread)
			{
				XSetThreadProcessor(thread, 5);		// Away from the main thread (hardware thread 0)
				ResumeThread(thread);
			}
		}

		void write(const char* text, bool endOfLine)
		{
			EnterCriticalSection(&mBufferLock);
			if (mLineStart)
			{
				char timestamp[24];
				const DWORD ms = GetTickCount() - mStartTicks;
				snprintf(timestamp, sizeof(timestamp), "[%5u.%03u] ", (unsigned)(ms / 1000), (unsigned)(ms % 1000));
				mPending += timestamp;
			}
			mPending += text;
			mLine += text;
			if (endOfLine)
			{
				mPending += "\r\n";
				DbgPrint("RSDKv4: %s\n", mLine.c_str());
				mLine.clear();
			}
			mLineStart = endOfLine;
			LeaveCriticalSection(&mBufferLock);
			if (endOfLine)
				SetEvent(mWakeUpEvent);
		}

		// Writes everything that's still buffered, from the calling thread (used when exiting or crashing)
		void flushNow()
		{
			writePending();
		}

	private:
		static DWORD WINAPI writerThreadFunction(LPVOID parameter)
		{
			AsyncLog& self = *static_cast<AsyncLog*>(parameter);
			while (true)
			{
				WaitForSingleObject(self.mWakeUpEvent, 1000);
				self.writePending();
			}
			return 0;
		}

		void writePending()
		{
			std::string text;
			EnterCriticalSection(&mBufferLock);
			text.swap(mPending);
			LeaveCriticalSection(&mBufferLock);
			if (text.empty() || mFile == INVALID_HANDLE_VALUE)
				return;

			EnterCriticalSection(&mFileLock);
			if (mBytesWritten < MAX_FILE_BYTES)
			{
				if (mBytesWritten + text.size() > MAX_FILE_BYTES)
					text = "\r\n[log size limit reached]\r\n";
				DWORD written = 0;
				WriteFile(mFile, text.data(), (DWORD)text.size(), &written, nullptr);
				FlushFileBuffers(mFile);		// Keeps the log useful after a crash; only the background thread waits for it
				mBytesWritten += text.size();
			}
			LeaveCriticalSection(&mFileLock);
		}

	private:
		HANDLE mFile = INVALID_HANDLE_VALUE;
		HANDLE mWakeUpEvent = nullptr;
		CRITICAL_SECTION mBufferLock;
		CRITICAL_SECTION mFileLock;
		std::string mPending;
		std::string mLine;			// For DbgPrint, which gets whole lines
		bool mLineStart = true;
		DWORD mStartTicks = 0;
		size_t mBytesWritten = 0;
	};

	AsyncLog& getLog()
	{
		static AsyncLog log("game:\\rsdkv4.log");
		return log;
	}

	void logLine(const char* format, ...)
	{
		char buffer[512];
		va_list args;
		va_start(args, format);
		vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);
		getLog().write(buffer, true);
	}

	bool isStackAddress(DWORD address)
	{
		return (address >= 0x70000000 && address < 0x80000000 && (address & 7) == 0);
	}

	// Logs the exception and a stack back-trace before the dashboard's crash handler takes over.
	// Symbolize the addresses with: python -I x360\tools\addr2sym.py <build output>\default.elf --log rsdkv4.log
	LONG WINAPI crashFilter(EXCEPTION_POINTERS* info)
	{
		const EXCEPTION_RECORD* record = info->ExceptionRecord;
		const CONTEXT* context = info->ContextRecord;
		logLine("!!! CRASH: exception 0x%08X at 0x%08X (thread %u)", (unsigned)record->ExceptionCode, (unsigned)(uintptr_t)record->ExceptionAddress, (unsigned)GetCurrentThreadId());
		for (DWORD i = 0; i < record->NumberParameters && i < 4; ++i)
			logLine("!!!   info[%u] = 0x%08X", (unsigned)i, (unsigned)record->ExceptionInformation[i]);
		logLine("!!!   IAR 0x%08X  LR 0x%08X  SP 0x%08X", (unsigned)context->Iar, (unsigned)context->Lr, (unsigned)context->Gpr1);
		logLine("!!!   r3 0x%08X  r4 0x%08X  r5 0x%08X  r31 0x%08X", (unsigned)context->Gpr3, (unsigned)context->Gpr4, (unsigned)context->Gpr5, (unsigned)context->Gpr31);

		// Walk the back chain: [sp] = caller's sp, the return address is saved at caller_sp - 8.
		// MS-compiled frames store both as 32-bit words (stwu/stw), clang frames as 64-bit doublewords
		// (stdu/std), where the 32-bit value sits in the low word at +4 and the high word is 0.
		auto readSlot = [](DWORD address) -> DWORD
		{
			const DWORD high = *reinterpret_cast<const DWORD*>(address);
			return (high != 0) ? high : *reinterpret_cast<const DWORD*>(address + 4);
		};
		DWORD stackPointer = (DWORD)context->Gpr1;
		for (int depth = 0; depth < 16 && isStackAddress(stackPointer); ++depth)
		{
			const DWORD callerStackPointer = readSlot(stackPointer);
			if (!isStackAddress(callerStackPointer) || callerStackPointer <= stackPointer)
				break;
			const DWORD returnAddress = readSlot(callerStackPointer - 8);
			logLine("!!!   frame %2d: return 0x%08X  (sp 0x%08X)", depth, (unsigned)returnAddress, (unsigned)callerStackPointer);
			stackPointer = callerStackPointer;
		}

		getLog().flushNow();
		return EXCEPTION_CONTINUE_SEARCH;
	}

	void returnToDashboard()
	{
		logLine("[X360] Exiting, returning to the dashboard");
		getLog().flushNow();
		XLaunchNewImage(XLAUNCH_KEYWORD_DEFAULT_APP, 0);
	}

	struct X360ProcessSetup
	{
		X360ProcessSetup()
		{
			logLine("[X360] " X360_GAME_NAME " (RSDKv4) for Xbox 360 starting");
			SetUnhandledExceptionFilter(crashFilter);
			atexit(returnToDashboard);
		}
	};

	X360ProcessSetup gX360ProcessSetup;
}


void X360_WriteLog(const char* text, bool endOfLine)
{
	getLog().write(text, endOfLine);
}
