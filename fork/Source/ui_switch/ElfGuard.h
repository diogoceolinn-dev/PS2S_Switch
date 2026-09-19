// Fase 2.1: guarda de sanidade para ELF antes do BootFromFile.
// O parser (CElfFileContainer) faz new uint8[tamanho] sem validar: um ISO
// de GBs no lugar do .elf (ou lixo) derruba o console em vez de dar erro
// limpo. Checamos: existe, <= 64 MB, mágico \x7FELF, máquina MIPS, tipo EXEC.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ELFGUARD_MAX_BYTES (64u * 1024u * 1024u)
#define ELFGUARD_EM_MIPS 8u
#define ELFGUARD_ET_EXEC 2u

static inline bool ElfGuard_Check(const char* path, char* errBuf, size_t errSize)
{
	FILE* f = fopen(path, "rb");
	if(f == nullptr)
	{
		snprintf(errBuf, errSize, "arquivo ausente: %s", path);
		return false;
	}
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if((size <= 0) || ((uint64_t)size > ELFGUARD_MAX_BYTES))
	{
		snprintf(errBuf, errSize, "tamanho invalido (%ld bytes, teto 64MB): %s", size, path);
		fclose(f);
		return false;
	}
	uint8_t hdr[20];
	size_t got = fread(hdr, 1, sizeof(hdr), f);
	fclose(f);
	if(got != sizeof(hdr))
	{
		snprintf(errBuf, errSize, "arquivo curto demais (%s)", path);
		return false;
	}
	if(!((hdr[0] == 0x7F) && (hdr[1] == 'E') && (hdr[2] == 'L') && (hdr[3] == 'F')))
	{
		snprintf(errBuf, errSize, "sem magico ELF (ISO renomeado? lixo?): %s", path);
		return false;
	}
	uint16_t machine = (uint16_t)(hdr[18] | (hdr[19] << 8));
	if(machine != ELFGUARD_EM_MIPS)
	{
		snprintf(errBuf, errSize, "ELF nao-MIPS (machine=%u): %s", (unsigned)machine, path);
		return false;
	}
	uint16_t type = (uint16_t)(hdr[16] | (hdr[17] << 8));
	if(type != ELFGUARD_ET_EXEC)
	{
		snprintf(errBuf, errSize, "ELF nao-executavel (type=%u): %s", (unsigned)type, path);
		return false;
	}
	return true;
}
