/* z64dump4.c		Zelda64 Dump v4
 * author:		SoulofDeity (Bitskit)
 *************************************************************************/
#include <malloc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#endif


#ifdef _WIN32
#define mkdir(dir) _mkdir(dir)
#else
#define mkdir(dir) mkdir(dir, 0777 & ~umask(0))
#endif


#define READ32(d)		((*(d) << 24) | (*((d)+1) << 16) | (*((d)+ 2) << 8) | (*((d)+3)))
#define READFTE(f, d)		{ (f)->virtual.start = READ32((d));      \
                                  (f)->virtual.end = READ32((d) + 4);    \
                                  (f)->physical.start = READ32((d) + 8); \
                                  (f)->physical.end = READ32((d) + 12); }


typedef struct
  {
    struct
      {
        uint32_t start, end;
      } physical, virtual;
    uint8_t *data;
    uint32_t size;
  } FileTableEntry;

typedef struct
  {
    char *filename;
    uint8_t isMM, steSize, oc, ac, sc, *data;
    uint32_t size, fileNameTable;
    struct
      {
        uint32_t start, size;
      } fileTable, sceneTable, objectTable, actorTable;
  } Rom;


enum
  {
    ZDATA		= 0,
    ZASM		= 1,
    ZACTOR		= 2,
    ZOBJ		= 4,
    ZSCENE		= 5,
    ZMAP		= 6,
    ZTXT		= 7
  };



static Rom rom = {0};
static FileTableEntry code = {0}, actor[3], object[3], scene[3];


static void Byteswap(void);
static void CheckRomType(void);
static void LocateFileTable(void);
static void LocateFileNameTable(void);
static void LocateCodeFile(void);
static void LocateSceneTable(void);
static void LocateObjectTable(void);
static void LocateActorTable(void);
static void ExtractScenesAndMaps(void);
static void ExtractActors(void);
static void ExtractObjects(void);

static void yaz0dec(uint8_t *src, uint8_t *dst, uint32_t size);

static int GetFile(FileTableEntry *fte, int32_t index);
static int GetFileNumber(uint32_t start, uint32_t end);
static void GetFileName(char *buffer, int32_t index);
static int GetFileType(uint8_t *data, uint32_t size);



int main(int argc, char *argv[])
  {
    printf("z64dump4 by SoulofDeity\n---------------------------------------------\n");
    int i;
    for (i = 1; i < argc; i++)
      {
        if (*argv[i] == '-')
          {
            if (!strcmp(argv[i], "--help"))
              {
                printf("Usage:  z64dump4 [options] romfile\n"
                       "\n"
                       "Options Include:\n"
                       "    --help  shows this message\n");
                return 0;
              }
            else
              {
                printf("ERROR:  Invalid options '%s'\n", argv[i]);
                return 0;
              }
          }
        else if (!rom.filename)
          {
            rom.filename = argv[i];
          }
        else
          {
            printf("ERROR: Invalid argument '%s'\n", argv[i]);
            return 0;
          }
      }
    if (!rom.filename)
      {
        printf("ERROR: No rom file specified\n");
        return 0;
      }
    FILE *fp = fopen(rom.filename, "rb");
    if (!fp)
      {
        printf("ERROR: Failed to open '%s'\n", rom.filename);
        return 0;
      }
    fseek(fp, 0, SEEK_END);
    rom.size = ftell(fp);
    rom.data = (uint8_t *) malloc(rom.size);
    if (!rom.data)
      {
        printf("ERROR:  Failed to allocate memory\n");
        fclose(fp);
        return 0;
      }
    rewind(fp);
    fread(rom.data, 1, rom.size, fp);
    fclose(fp);
    Byteswap();
    CheckRomType();
    LocateFileTable();
    LocateCodeFile();
    LocateFileNameTable();
    LocateSceneTable();
    LocateObjectTable();
    LocateActorTable();
    ExtractScenesAndMaps();
    ExtractActors();
    ExtractObjects();
    if (code.physical.end)
      free(code.data);
    free(rom.data);
    return 0;
  }



static void error()
  {
    if (code.physical.end)
      free(code.data);
    free(rom.data);
    exit(0);
  }


static void Byteswap(void)
  {
    printf("byteswapping...              ");
    switch (READ32(&rom.data[0]))
      {
        case 0x80371240:
          {
            printf("ok\n");
            return;
          }
        case 0x40123780:
          {
            printf("little endian");
            uint32_t i, c;
            for (i = 0; i < rom.size; i += 4)
              {
                c = rom.data[i];
                rom.data[i] = rom.data[i + 3];
                rom.data[i + 3] = c;
                c = rom.data[i + 1];
                rom.data[i + 1] = rom.data[i + 2];
                rom.data[i + 2] = c;
              }
            break;
          }
        case 0x37804012:
          {
            printf("middle endian");
            uint32_t i, c;
            for (i = 0; i < rom.size; i += 4)
              {
                c = rom.data[i];
                rom.data[i] = rom.data[i + 1];
                rom.data[i + 1] = c;
                c = rom.data[i + 2];
                rom.data[i + 2] = rom.data[i + 3];
                rom.data[i + 3] = c;
              }
            break;
          }
        default:
          {
            printf("error: unknown endianness\n");
            error();
          }
      }
    printf(" -> big endian\n");
  }


static void CheckRomType(void)
  {
    printf("checking rom type...         ");
    rom.steSize = 0x14;
    if (!memcmp(&rom.data[0x20], "ZELDA MAJORA'S MASK ", 20))
      {
        printf("Majora's Mask\n");
        rom.isMM = 1;
        rom.steSize -= 4;
      }
    else
      printf("Ocarina of Time\n");
  }


static void LocateFileTable(void)
  {
    printf("locating file table...       ");
    uint32_t i, found = 0;
    FileTableEntry fte;
    for (i = 0; i < rom.size; i += 16)
      {
        READFTE(&fte, &rom.data[i]);
        if (!found && !fte.virtual.start && fte.virtual.end == 0x00001060 &&
            !fte.physical.start && !fte.physical.end)
          found++;
        else if (found)
          {
            if (fte.virtual.start == 0x00001060 && fte.virtual.end &&
                fte.physical.start == 0x00001060 && !fte.physical.end)
              {
                rom.fileTable.start = i - 16;
                READFTE(&fte, &rom.data[i + 16]);
                rom.fileTable.size = fte.virtual.end - fte.virtual.start;
                break;
              }
            else
              found = 0;
          }
      }
    if (found)
      printf("found [%08X, %i files]\n", rom.fileTable.start, rom.fileTable.size / 16);
    else
      {
        printf("error: not found\n");
        error();
      }
  }


static void LocateCodeFile(void)
  {
    printf("locating code file...        ");
    uint32_t i = 0;
    FileTableEntry fte;
    for (i = 0; i < rom.fileTable.size / 16; i++)
      {
        if (GetFile(&fte, i))
          {
            switch (GetFileType(fte.data, fte.size))
              {
                case ZASM:
                  {
                    if (!code.virtual.start)
                      memcpy(&code, &fte, sizeof(FileTableEntry));
                    else if (fte.physical.end)
                      free(fte.data);
                    break;
                  }
                case ZACTOR:
                  {
                    if (rom.ac < 3)
                      memcpy(&actor[rom.ac++], &fte, 16);
                    else
                      rom.ac++;
                    if (fte.physical.end)
                      free(fte.data);
                    break;
                  }
                case ZOBJ:
                  {
                    if (rom.oc < 3)
                      memcpy(&object[rom.oc++], &fte, 16);
                    else
                      rom.oc++;
                    if (fte.physical.end)
                      free(fte.data);
                    break;
                  }
                case ZSCENE:
                  {
                    if (rom.sc < 3)
                      memcpy(&scene[rom.sc++], &fte, 16);
                    else
                      rom.sc++;
                    if (fte.physical.end)
                      free(fte.data);
                    break;
                  }
              }
            if (rom.ac >= 3 && rom.oc >= 3 && rom.sc >= 3)
              break;
          }
      }
    if (code.virtual.start)
      printf("found [%08X - %08X]\n", code.virtual.start, code.virtual.end);
    else
      {
        printf("error: not found\n");
        error();
      }
  }


static void LocateFileNameTable(void)
  {
    printf("locating file name table...  ");
    uint32_t i;
    for (i = 0; i < rom.size - 4; i += 4)
      {
        if (READ32(&rom.data[i]) == 0x6D616B65 && READ32(&rom.data[i + 4]) == 0x726F6D00)
          {
            rom.fileNameTable = i;
            printf("found [%08X]\n", rom.fileNameTable);
            return;
          }
      }
    printf("N/A\n");
  }


static void LocateSceneTable(void)
  {
    printf("locating scene table...      ");
    uint32_t i, w0, w1;
    FileTableEntry fte;
    if (rom.sc < 3)
      {
        printf("error: not found\n");
        error();
      }
    scene[0].physical.start = scene[1].physical.start = scene[2].physical.start = 0;
    for (i = 0; i < code.size - 4; i += 4)
      {
        w0 = READ32(&code.data[i]);
        w1 = READ32(&code.data[i + 4]);
        if (w0 == scene[0].virtual.start && w1 == scene[0].virtual.end)
          {
            scene[0].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == scene[1].virtual.start && w1 == scene[1].virtual.end)
          {
            scene[1].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == scene[2].virtual.start && w1 == scene[2].virtual.end)
          {
            scene[2].physical.start = i;
            i += rom.steSize - 4;
          }
        if (scene[0].physical.start && scene[1].physical.start && scene[2].physical.start)
          break;
      }
    if (!scene[0].physical.start && !scene[1].physical.start && !scene[2].physical.start)
      {
        printf("error: not found\n");
        error();
      }
    scene[0].physical.end = scene[0].physical.start < scene[1].physical.start ?
                            scene[0].physical.start : scene[1].physical.start;
    scene[0].physical.end = scene[0].physical.end < scene[2].physical.start ?
                            scene[0].physical.end : scene[2].physical.start;
    scene[2].physical.end = scene[0].physical.start > scene[1].physical.start ?
                            scene[0].physical.start : scene[1].physical.start;
    scene[2].physical.end = scene[2].physical.end > scene[2].physical.start ?
                            scene[2].physical.end : scene[2].physical.start;
    if (scene[0].physical.start > scene[0].physical.end &&
        scene[0].physical.start < scene[2].physical.end)
      scene[1].physical.end = scene[0].physical.start;
    else if (scene[1].physical.start > scene[0].physical.end &&
             scene[1].physical.start < scene[2].physical.end)
      scene[1].physical.end = scene[1].physical.start;
    else if (scene[2].physical.start > scene[0].physical.end &&
             scene[2].physical.start < scene[2].physical.end)
      scene[1].physical.end = scene[2].physical.start;
    if (!scene[0].physical.end)
      scene[0].physical.end = scene[1].physical.end;
    if (!scene[0].physical.end)
      scene[0].physical.end = scene[2].physical.end;
    if (!scene[0].physical.end ||
        ((scene[1].physical.end - scene[0].physical.end) % rom.steSize) ||
        ((scene[2].physical.end - scene[1].physical.end) % rom.steSize))
      {
        printf("error: not found\n");
        error();
      }
    i = scene[0].physical.end;
    while (i)
      {
        w0 = READ32(&code.data[i]);
        w1 = READ32(&code.data[i + 4]);
        if (!w0 || GetFileNumber(w0, w1) >= 0)
          i -= rom.steSize;
        else
          break;
      }
    rom.sceneTable.start = i + rom.steSize;
    printf("found [%08X]\n", code.virtual.start + rom.sceneTable.start);
  }


static void LocateObjectTable(void)
  {
    printf("locating object table...     ");
    uint32_t i, w0, w1;
    FileTableEntry fte;
    if (rom.oc < 3)
      {
        printf("error: not found\n");
        error();
      }
    object[0].physical.start = object[1].physical.start = object[2].physical.start = 0;
    for (i = 0; i < code.size - 4; i += 4)
      {
        w0 = READ32(&code.data[i]);
        w1 = READ32(&code.data[i + 4]);
        if (w0 == object[0].virtual.start && w1 == object[0].virtual.end)
          {
            object[0].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == object[1].virtual.start && w1 == object[1].virtual.end)
          {
            object[1].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == object[2].virtual.start && w1 == object[2].virtual.end)
          {
            object[2].physical.start = i;
            i += rom.steSize - 4;
          }
        if (object[0].physical.start && object[1].physical.start && object[2].physical.start)
          break;
      }
    if (!object[0].physical.start)
      object[0] = object[1];
    if (!object[0].physical.start)
      object[0] = object[2];
    if (!object[0].physical.start)
      {
        printf("error: not found\n");
        error();
      }
    i = object[0].physical.start;
    while (i)
      {
        w0 = READ32(&code.data[i]);
        if (!w0 || GetFileNumber(w0, 0) >= 0)
          i -= 8;
        else
          break;
      }
    rom.objectTable.start = i + 8;
    printf("found [%08X]\n", code.virtual.start + rom.objectTable.start);
  }


static void LocateActorTable(void)
  {
    printf("locating actor table...      ");
    uint32_t i, w0, w1;
    FileTableEntry fte;
    if (rom.ac < 3)
      {
        printf("error: not found\n");
        error();
      }
    actor[0].physical.start = actor[1].physical.start = actor[2].physical.start = 0;
    for (i = 0; i < code.size - 4; i += 4)
      {
        w0 = READ32(&code.data[i]);
        w1 = READ32(&code.data[i + 4]);
        if (w0 == actor[0].virtual.start && w1 == actor[0].virtual.end)
          {
            actor[0].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == actor[1].virtual.start && w1 == actor[1].virtual.end)
          {
            actor[1].physical.start = i;
            i += rom.steSize - 4;
          }
        else if (w0 == actor[2].virtual.start && w1 == actor[2].virtual.end)
          {
            actor[2].physical.start = i;
            i += rom.steSize - 4;
          }
        if (actor[0].physical.start && actor[1].physical.start && actor[2].physical.start)
          break;
      }
    if (!actor[0].physical.start)
      actor[0] = actor[1];
    if (!actor[0].physical.start)
      actor[0] = actor[2];
    if (!actor[0].physical.start)
      {
        printf("error: not found\n");
        error();
      }
    i = actor[0].physical.start;
    while (i)
      {
        w0 = READ32(&code.data[i]);
        if (!w0 || GetFileNumber(w0, 0) >= 0)
          i -= 32;
        else
          break;
      }
    rom.actorTable.start = i + 32;
    printf("found [%08X]\n", code.virtual.start + rom.actorTable.start);
  }


static void ExtractScenesAndMaps(void)
  {
    printf("extracting scenes and maps...");
    uint32_t i, j, w0, w1, start, end, totalScenes = 0, totalMaps = 0;
    int32_t fileNum;
    char filePath[512];
    FILE *fp;
    FileTableEntry fte, map;
    mkdir("data");
    mkdir("data/scenes");
    for (i = rom.sceneTable.start; i < code.size; i += rom.steSize)
      {
        start = READ32(&code.data[i]);
        end = READ32(&code.data[i + 4]);
        if (start && (fileNum = GetFileNumber(start, end)) >= 0)
          {
            if (GetFile(&fte, fileNum))
              {
                sprintf(filePath, "data/scenes/%03i", (i - rom.sceneTable.start)  / rom.steSize);
                mkdir(filePath);
                filePath[15] = '/';
                GetFileName(&filePath[16], fileNum);
                sprintf(&filePath[strlen(filePath)], ".zscene");
                if (fp = fopen(filePath, "wb"))
                  {
                    fwrite(fte.data, 1, fte.size, fp);
                    fclose(fp);
                    totalScenes++;
                  }
                sprintf(&filePath[15], "/maps");
                for (j = 0; j + 7 < fte.size; j += 8)
                  {
                    w0 = READ32(&fte.data[j]);
                    w1 = READ32(&fte.data[j + 4]);
                    if ((w0 & 0xFF000000) == 0x04000000)
                      {
                        uint32_t k, mapCount = (w0 >> 16) & 0xFF;
                        uint32_t mapList = w1 & 0x00FFFFFF;
                        totalMaps += mapCount;
                        for (k = 0; k < mapCount; k++)
                          {
                            w0 = READ32(&fte.data[mapList + k * 8]);
                            w1 = READ32(&fte.data[mapList + k * 8 + 4]);
                            fileNum = GetFileNumber(w0, 0);
                            if (fileNum >= 0 && GetFile(&map, fileNum))
                              {
                                filePath[20] = 0;
                                mkdir(filePath);
                                filePath[20] = '/';
                                GetFileName(&filePath[21], fileNum);
                                sprintf(&filePath[strlen(filePath)], ".zmap");
                                if (fp = fopen(filePath, "wb"))
                                  {
                                    fwrite(map.data, 1, map.size, fp);
                                    fclose(fp);
                                  }
                                if (map.physical.end)
                                  free(map.data);
                              }
                          }
                      }
                    else if (w0 == 0x14000000)
                      break;
                  }
                if (fte.physical.end)
                  free(fte.data);
              }
          }
        else if (start)
          break;
      }
    printf("ok    [%i/%i scenes, %i maps]\n", totalScenes, (i - rom.sceneTable.start)  / rom.steSize, totalMaps);
  }


static void ExtractActors(void)
  {
    printf("extracting actors...         ");
    uint32_t i, j, w0, w1, start, end, info, totalActors = 0;
    int32_t fileNum;
    char filePath[512];
    FILE *fp;
    FileTableEntry fte, map;
    mkdir("data");
    mkdir("data/actors");
    strcpy(filePath, "data/actors/");
    for (i = rom.actorTable.start; i < code.size; i += 32)
      {
        start = READ32(&code.data[i]);
        end = READ32(&code.data[i + 4]);
        info = READ32(&code.data[i + 20]) - READ32(&code.data[i + 8]);
        if (start && (fileNum = GetFileNumber(start, end)) >= 0)
          {
            if (GetFile(&fte, fileNum))
              {
                filePath[12] = 0;
                switch (fte.data[info + 2]) {
                    case 1:
                        strcat(filePath, "props (1)");
                        break;
                    case 2:
                        strcat(filePath, "player");
                        break;
                    case 3:
                        strcat(filePath, "bombs");
                        break;
                    case 4:
                        strcat(filePath, "npcs");
                        break;
                    case 5:
                        strcat(filePath, "enemies");
                        break;
                    case 6:
                        strcat(filePath, "props (2)");
                        break;
                    case 7:
                        strcat(filePath, "items and actions");
                        break;
                    case 8:
                        strcat(filePath, "misc");
                        break;
                    case 9:
                        strcat(filePath, "bosses");
                        break;
                    case 10:
                        strcat(filePath, "doors");
                        break;
                    case 11:
                        strcat(filePath, "chests");
                        break;
                    default:
                        sprintf(filePath + strlen(filePath), "unknown group %02X", fte.data[info + 2]);
                        break;
                }
                mkdir(filePath);
                sprintf(filePath + strlen(filePath), "/%03X [obj %03X] - ", (i - rom.actorTable.start) / 32, (fte.data[info + 8] << 8) | fte.data[info + 9]);
                GetFileName(filePath + strlen(filePath), fileNum);
                sprintf(filePath + strlen(filePath), ".zactor");
                if (fp = fopen(filePath, "wb"))
                  {
                    fwrite(fte.data, 1, fte.size, fp);
                    fclose(fp);
                    totalActors++;
                  }
                if (fte.physical.end)
                  free(fte.data);
              }
          }
        else if (start)
          break;
      }
    printf("ok    [%i/%i actors]\n", totalActors, (i - rom.actorTable.start)  / 32);
  }


static void ExtractObjects(void)
  {
    printf("extracting objects...        ");
    uint32_t i, j, w0, w1, start, end, totalObjects = 0;
    int32_t fileNum;
    char filePath[512];
    FILE *fp;
    FileTableEntry fte, map;
    mkdir("data");
    mkdir("data/objects");
    for (i = rom.objectTable.start; i < code.size; i += 8)
      {
        start = READ32(&code.data[i]);
        end = READ32(&code.data[i + 4]);
        if (start && (fileNum = GetFileNumber(start, end)) >= 0)
          {
            if (GetFile(&fte, fileNum))
              {
                if ((i - rom.objectTable.start) / 8 == 1)
                  {
                    sprintf(filePath, "data/gk_%03X - ", (i - rom.objectTable.start) / 8);
                    GetFileName(&filePath[14], fileNum);
                  }
                else if ((i - rom.objectTable.start) / 8 == 2)
                  {
                    sprintf(filePath, "data/fk_%03X - ", (i - rom.objectTable.start) / 8);
                    GetFileName(&filePath[14], fileNum);
                  }
                else if ((i - rom.objectTable.start) / 8 == 3)
                  {
                    sprintf(filePath, "data/dk_%03X - ", (i - rom.objectTable.start) / 8);
                    GetFileName(&filePath[14], fileNum);
                  }
                else
                  {
                    sprintf(filePath, "data/objects/%03X - ", (i - rom.objectTable.start) / 8);
                    GetFileName(&filePath[19], fileNum);
                  }
                sprintf(&filePath[strlen(filePath)], ".zobj");
                if (fp = fopen(filePath, "wb"))
                  {
                    fwrite(fte.data, 1, fte.size, fp);
                    fclose(fp);
                    totalObjects++;
                  }
                if (fte.physical.end)
                  free(fte.data);
              }
          }
        else if (start)
          break;
      }
    printf("ok    [%i/%i objects]\n", totalObjects, (i - rom.objectTable.start) / 8);
  }


static void yaz0dec(uint8_t *src, uint8_t *dst, uint32_t size)
  {
    uint32_t srcPos = 0, dstPos = 0, cpyPos, cpyLen, vb = 1, cb;
    while (dstPos < size)
      {
        if (cb <<= 1, vb--, !vb)
          {
            cb = src[srcPos++];
            vb = 8;
          }
        if (cb & 0x80)
          dst[dstPos++] = src[srcPos++];
        else
          {
            cpyLen = src[srcPos++];
            cpyPos = dstPos - (((cpyLen & 0x0F) << 8) | src[srcPos++]) - 1;
            cpyLen = cpyLen >> 4 ? (cpyLen >> 4) + 2 : src[srcPos++] + 0x12;
            for(; cpyLen; cpyLen--)
              dst[dstPos++] = dst[cpyPos++];
          }
      }
  }


static int GetFile(FileTableEntry *fte, int32_t index)
  {
    if (index < 0 || index >= rom.fileTable.size / 16)
      return 0;
    READFTE(fte, &rom.data[rom.fileTable.start + index * 16]);
    if (fte->virtual.end && fte->physical.end && fte->physical.start != 0xFFFFFFFF &&
        fte->physical.end != 0xFFFFFFFF)
      {
        if (!strncmp(&rom.data[fte->physical.start], "Yaz0", 4))
          {
            fte->size = READ32(&rom.data[fte->physical.start + 4]);
            fte->data = (uint8_t *) malloc(fte->size);
            if (fte->data)
              yaz0dec(&rom.data[fte->physical.start + 0x10], fte->data, fte->size);
            else
              return 0;
            fte->physical.end = 0xFFFFFFFF;
          }
        else
          {
            fte->size = fte->virtual.end - fte->virtual.start;
            fte->data = &rom.data[fte->physical.start];
            fte->physical.end = 0x00000000;
          }
      }
    else if (fte->virtual.end && fte->physical.start != 0xFFFFFFFF &&
             (!fte->physical.end || (fte->physical.end && fte->physical.end != 0xFFFFFFFF &&
             (fte->physical.end - fte->physical.start == fte->virtual.end - fte->virtual.start))))
      {
        fte->size = fte->virtual.end - fte->virtual.start;
        fte->data = &rom.data[fte->physical.start];
        fte->physical.end = 0x00000000;
      }
    else
      return 0;
    return 1;
  }


static int GetFileNumber(uint32_t start, uint32_t end)
  {
    int i;
    FileTableEntry fte;
    for (i = rom.fileTable.start; i < rom.fileTable.start + rom.fileTable.size; i += 16)
      {
        READFTE(&fte, &rom.data[i]);
        if (fte.virtual.start == start && (!end || (end && fte.virtual.end == end)))
          return (i - rom.fileTable.start) / 16;
      }
    return -1;
  }


static void GetFileName(char *buffer, int32_t index)
  {
    if (index < 0 || index >= rom.fileTable.size / 16)
      {
        *buffer = 0;
        return;
      }
    uint32_t i, j = 0;
    FileTableEntry fte;
    if (rom.fileNameTable)
      {
        for (i = 0; i < rom.fileTable.size / 16; i++)
          {
            for (; !rom.data[rom.fileNameTable + j]; j++);
            if (i == index)
              {
                sprintf(buffer, "%s", &rom.data[rom.fileNameTable + j]);
                break;
              }
            else
              {
                for (; rom.data[rom.fileNameTable + j]; j++);
              }
          }
      }
    else
      {
        READFTE(&fte, &rom.data[rom.fileTable.start + index * 16]);
        sprintf(buffer, "%08X - %08X", fte.virtual.start, fte.virtual.end);
      }
  }


static int GetFileType(uint8_t *data, uint32_t size)
  {
    const uint8_t codesig[] =
      {
        0x18, 0xF9, 0x6A, 0x6E, 0xB8, 0xE3, 0x82, 0x76,
        0x47, 0x1D, 0x18, 0xF9, 0x82, 0x76, 0x6A, 0x6E,
        0x6A, 0x6E, 0x82, 0x76, 0xE7, 0x07, 0xB8, 0xE3,
        0x7D, 0x8A, 0x47, 0x1D, 0x6A, 0x6E, 0x18, 0xF9
      };
    if (size < 8)
      return ZDATA;
    if (size > 32 && !memcmp(&data[size - 32], codesig, 32))
      return ZASM;
    uint32_t tlen = READ32(&data[size - 4]);
    if (tlen >= 0x18 && tlen < size)
      {
        uint32_t table = size - tlen;
        uint32_t textSize = READ32(&data[table]);
        uint32_t dataSize = READ32(&data[table + 4]);
        uint32_t rodataSize = READ32(&data[table + 8]);
        uint32_t bssSize = READ32(&data[table + 12]);
        uint32_t numRelocs = READ32(&data[table + 16]);
        uint32_t actorSize = 0x28 + textSize + dataSize + rodataSize + bssSize + numRelocs * 4;
        if ((actorSize - (actorSize % 0x10)) == size)
          return ZACTOR;
      }
    uint32_t i, flags = 0, w0, w1;
    for (i = 0; i < size && i < size; i += 8)
      {
        w0 = READ32(&data[i]);
        w1 = READ32(&data[i + 4]);
        if (i < 0x100 && w0 == 0x14000000 && !w1)
          {
            if (flags & 0x80000000 && !(flags & ~0xE0000000))
              return ZSCENE;
            return ZMAP;
          }
        else if (data[i] == 0x04)
          flags |= 0x80000000;
        else if ((data[i] == 0x05 && !w1) || data[i] == 0x06)
          flags |= 0x40000000;
        else if (data[i] == 0x01)
          flags |= 0x20000000;
        else if (w0 == 0xDF000000 && !w1)
          return ZOBJ;
      }
    return ZDATA;
  }
