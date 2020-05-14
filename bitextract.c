#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define mkdir(dir, mode) _mkdir(dir)
#else
#include <sys/stat.h>
#endif

#pragma pack(push, 1)
typedef struct
{
    uint8_t  b[4];
    uint32_t length;
    uint16_t skip;
} cmpHeader;

typedef struct
{
    char     magic[4];
    uint16_t revision;
    uint32_t numEntries;
} bitHeader;

typedef struct
{
    uint32_t id;
    uint32_t offset;
    uint32_t length;
    uint32_t hash;
    uint8_t  flag;
} bitEntry;

#pragma pack(pop)

// type 0: uncompressed, raw data copy
int32_t decompressCopy(uint8_t *in, uint8_t **out)
{
	int32_t length;
	
    cmpHeader *hdr = (cmpHeader *)in;

    length = hdr->length - hdr->skip;

    *out = malloc(length);
    memcpy(*out, in + hdr->skip + sizeof(cmpHeader), length);

    return length;
}

// type 1: RLE compression
int32_t decompressRLE(uint8_t *in, uint8_t **out)
{
	int32_t bytesLeft;
	uint8_t *outPtr;
	
    cmpHeader *hdr = (cmpHeader *)in;

    bytesLeft = hdr->length - hdr->skip;

    outPtr = *out = malloc(bytesLeft);

    // skip past header
    in += hdr->skip + sizeof(cmpHeader);

    do
    {
        uint8_t  nr = *in;
        uint32_t copy;

        if (nr >= 0x80) // RLE mode
        {
            copy = nr - 0x7d; // count

            memset(outPtr, in[1] /* value */, copy);

            in        += 2;
            bytesLeft -= copy;
        }
        else // raw copy
        {
            copy = nr + 1;

            memcpy(outPtr, in + 1, copy);

            in        += copy + 1;
            bytesLeft -= copy;
        }
        
        outPtr += copy;
    }
    while (bytesLeft > 0);

    return hdr->length - hdr->skip;
}

// type 2: A fusion of LZ and RLE, switching modes depending on the bit(s) set in the operation byte
int32_t decompressLZRLE(uint8_t *in, uint8_t **out)
{
	int32_t bytesLeft;
	uint8_t *outPtr;

    cmpHeader *hdr = (cmpHeader *)in;

    bytesLeft = hdr->length - hdr->skip;

    outPtr = *out = malloc(bytesLeft);

    // skip past header
    in += hdr->skip + 10;

    do
    {
        uint8_t  op = *in;
        uint32_t copy, distance;

        if (op & 0x80)
        {
            if (op & 0x40) // RLE mode
            {
                copy = op - 0xbd; // count

                memset(outPtr, in[1] /* value */, copy);

                in        += 2;
                bytesLeft -= copy;
            }
            else // LZ mode - copies from sliding buffer
            {
                copy     = op - 0x7c;           // number of bytes to copy
                distance = *(uint16_t *)&in[1]; // uint16_t distance to copy from (out - distance)

                memcpy(outPtr, outPtr - distance, copy);

                in        += 3;
                bytesLeft -= copy;
            }
        }
        else // raw copy
        {
            copy = op + 1;

            memcpy(outPtr, in + 1, copy);

            in        += copy + 1;
            bytesLeft -= copy;
        }
        
        outPtr += copy;
    }
    while (bytesLeft > 0);

    return hdr->length - hdr->skip;
}

int32_t (*decompressionFuncs[3])(uint8_t *in, uint8_t **out) =
{
    &decompressCopy,
    &decompressRLE,
    &decompressLZRLE
};

int main(int argc, char **argv)
{
    char fn[256];
    bitHeader hdr;
    bitEntry *entries;
    FILE *in, *out;
    uint8_t *inB, *outB;
    int32_t sz;

    if (argc != 3)
    {
        printf("Usage: %s input.bit directory\n", argv[0]);
        return 0;
    }

    if ((in = fopen(argv[1], "rb")) == NULL)
    {
        perror("Error");
        return 1;
    }

    fread(&hdr, sizeof(bitHeader), 1, in);

    if (strncmp(hdr.magic, "BITP", 4))
    {
        fprintf(stderr, "Error: Invalid magic %02x%02x%02x%02x!\n",
            (uint8_t)hdr.magic[0], (uint8_t)hdr.magic[1],
            (uint8_t)hdr.magic[2], (uint8_t)hdr.magic[3]);
        return 1;
    }

    // allocate the number of entries
    entries = malloc(sizeof(bitEntry) * hdr.numEntries);
    fread(entries, sizeof(bitEntry), hdr.numEntries, in);

    mkdir(argv[2], 0777);

    // extract all file entries
    for (int i = 0; i < hdr.numEntries; i++)
    {
        sprintf(fn, "%s/%08x", argv[2], entries[i].id);

        if ((out = fopen(fn, "wb")) == NULL)
        {
            perror("Error");
            return 1;
        }

        inB = malloc(entries[i].length);

        fseek(in, entries[i].offset, SEEK_SET);
        fread(inB, 1, entries[i].length, in);

        if (*inB > 2)
        {
            fprintf(stderr, "Error: Unsupported compression format %d\n", *inB);
            return 1;
        }
        else
        {
            printf("Extracting %s...\n", fn);
        }

        sz = decompressionFuncs[*inB](inB, &outB);

        fwrite(outB, 1, sz, out);
        fclose(out);

        free(inB);
        free(outB);
    }

    free(entries);

    puts("\nDone!");

    return 0;
}