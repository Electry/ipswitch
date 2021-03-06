// Copyright 2017 plutoo

// Here is the ISC License that switchbrew/switch-tools is licensed under:

/* Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above 
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR 
 * IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "elf2nso.h"

uint8_t* ReadEntireFile(const char* fn, size_t* len_out) {
    FILE* fd = fopen(fn, "rb");
    if (fd == NULL)
        return NULL;

    fseek(fd, 0, SEEK_END);
    size_t len = ftell(fd);
    fseek(fd, 0, SEEK_SET);

    uint8_t* buf = malloc(len);
    if (buf == NULL) {
        fclose(fd);
        return NULL;
    }

    size_t rc = fread(buf, 1, len, fd);
    if (rc != len) {
        fclose(fd);
        free(buf);
        return NULL;
    }

    *len_out = len;
    return buf;
}

int elf2nso(uint8_t* elf, size_t elf_len, FILE* out) {
    NsoHeader nso_hdr;
    memset(&nso_hdr, 0, sizeof(nso_hdr));
    memcpy(nso_hdr.Magic, "NSO0", 4);
    nso_hdr.Unk3 = 0x3f;

    if (sizeof(NsoHeader) != 0x100) {
        fprintf(stderr, "Bad compile environment!\n");
        return EXIT_FAILURE;
    }

    if (elf == NULL) {
        fprintf(stderr, "Failed to open input!\n");
        return EXIT_FAILURE;
    }

    if (elf_len < sizeof(Elf64_Ehdr)) {
        fprintf(stderr, "Input file doesn't fit ELF header!\n");
        return EXIT_FAILURE;
    }

    Elf64_Ehdr* hdr = (Elf64_Ehdr*) elf;
    if (hdr->e_machine != EM_AARCH64) {
        fprintf(stderr, "Invalid ELF: expected AArch64!\n");
        return EXIT_FAILURE;
    }

    Elf64_Off ph_end = hdr->e_phoff + hdr->e_phnum * sizeof(Elf64_Phdr);

    if (ph_end < hdr->e_phoff || ph_end > elf_len) {
        fprintf(stderr, "Invalid ELF: phdrs outside file!\n");
        return EXIT_FAILURE;
    }

    Elf64_Phdr* phdrs = (Elf64_Phdr*) &elf[hdr->e_phoff];
    size_t i, j = 0;
    size_t file_off = sizeof(NsoHeader);

    uint8_t* comp_buf[3];
    int comp_sz[3];

    for (i=0; i<3; i++) {
        Elf64_Phdr* phdr = NULL;
        while (j < hdr->e_phnum) {
            Elf64_Phdr* cur = &phdrs[j++];
            if (cur->p_type == PT_LOAD) {
                phdr = cur;
                break;
            }
        }

        if (phdr == NULL) {
            fprintf(stderr, "Invalid ELF: expected 3 loadable phdrs!\n");
            return EXIT_FAILURE;
        }

        nso_hdr.Segments[i].FileOff = file_off;
        nso_hdr.Segments[i].DstOff = phdr->p_vaddr;
        nso_hdr.Segments[i].DecompSz = phdr->p_filesz;

        // for .data segment this field contains bss size
        if (i == 2)
            nso_hdr.Segments[i].AlignOrTotalSz = phdr->p_memsz - phdr->p_filesz;
        else
            nso_hdr.Segments[i].AlignOrTotalSz = 1;

        SHA256_CTX ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, &elf[phdr->p_offset], phdr->p_filesz);
        sha256_final(&ctx, (u8*) &nso_hdr.Hashes[i]);

        size_t comp_max = LZ4_compressBound(phdr->p_filesz);
        comp_buf[i] = malloc(comp_max);

        if (comp_buf[i] == NULL) {
            fprintf(stderr, "Compressing: Out of memory!\n");
            return EXIT_FAILURE;
        }

        // TODO check p_offset
        comp_sz[i] = LZ4_compress_default((char*)&elf[phdr->p_offset], (char*)comp_buf[i], phdr->p_filesz, comp_max);

        if (comp_sz[i] < 0) {
            fprintf(stderr, "Failed to compress!\n");
            return EXIT_FAILURE;
        }

        nso_hdr.CompSz[i] = comp_sz[i];
        file_off += comp_sz[i];
    }

    if (out == NULL) {
        fprintf(stderr, "Failed to open output file!\n");
        return EXIT_FAILURE;
    }

    // TODO check retvals
    fwrite(&nso_hdr, sizeof(nso_hdr), 1, out);

    for (i=0; i<3; i++)
        fwrite(comp_buf[i], comp_sz[i], 1, out);

    return EXIT_SUCCESS;
}
