#ifndef PROC_H
#define PROC_H
#pragma once

#include <Windows.h>
#include <string>
#include <vector>



class Proc
{
public:

	BOOLEAN OnSetup(std::string ProcessName);
	~Proc();


	BOOLEAN ReadProcessMemory(PVOID Address, DWORD Size, PVOID Dst);
	template <typename T, typename U>
	T Read(U Address)
	{
		T Buff{ 0 };
		ReadProcessMemory((PVOID)Address, sizeof(T), &Buff);
		return Buff;
	}

	BOOLEAN WriteProcessMemory(PVOID Address, DWORD Size, PVOID Src);
	template <typename T, typename U>
	BOOLEAN Write(U Address, T Val)
	{
		return WriteProcessMemory((PVOID)Address, sizeof(T), &Val);
	}


private:
	BOOLEAN GetProcessId();
	BOOLEAN Callback(PVOID Block, PVOID PhysBlock, ULONG BlockSize, PVOID Context);
	uint64_t TranslateVirtualAddress(uint64_t directoryTableBase, LPVOID virtualAddress);

private:
	std::string m_ProcessName;
	uint64_t m_ProcessId;
	uint64_t m_DirectoryTable;
	uint8_t* m_PhysEprocess;
	uint64_t m_VaPEB;
};

#endif // !PROC_H

extern Proc* g_pProc;