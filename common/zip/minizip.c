/*
minizip.c
Version 1.01e, February 12th, 2005

Copyright (C) 1998-2005 Gilles Vollant
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>

#ifdef unix
# include <unistd.h>
# include <utime.h>
# include <sys/types.h>
# include <sys/stat.h>
#else
# include <direct.h>
# include <io.h>
#endif

#include "zip.h"

#ifdef WIN32
#define USEWIN32IOAPI
#include "iowin32.h"
#endif



#define WRITEBUFFERSIZE (16384)
#define MAXFILENAME (256)
//获取文件时间
uLong filetime(char *f, uLong *dt)
{
    int ret = 0;
    {
        FILETIME ftLocal;
        HANDLE hFind;
        WIN32_FIND_DATA  ff32;

        hFind = FindFirstFile(f, &ff32);
        if (hFind != INVALID_HANDLE_VALUE)
        {
            FileTimeToLocalFileTime(&(ff32.ftLastWriteTime), &ftLocal);
            FileTimeToDosDateTime(&ftLocal, ((LPWORD)dt) + 1, ((LPWORD)dt) + 0);
            FindClose(hFind);
            ret = 1;
        }
    }
    return ret;
}
//计算文件crc
int getFileCrc(const char* filenameinzip, void*buf, unsigned long size_buf, unsigned long* result_crc)
{
    unsigned long calculate_crc = 0;
    int err = ZIP_OK;
    FILE * fin = fopen(filenameinzip, "rb");
    unsigned long size_read = 0;
    unsigned long total_read = 0;
    if (fin == NULL)
    {
        err = ZIP_ERRNO;
    }

    if (err == ZIP_OK)
        do
        {
            err = ZIP_OK;
            size_read = (int)fread(buf, 1, size_buf, fin);
            if (size_read < size_buf)
                if (feof(fin) == 0)
                {
                    printf("error in reading %s\n", filenameinzip);
                    err = ZIP_ERRNO;
                }

            if (size_read > 0)
                calculate_crc = crc32(calculate_crc, buf, size_read);
            total_read += size_read;

        } while ((err == ZIP_OK) && (size_read > 0));

        if (fin)
            fclose(fin);

        *result_crc = calculate_crc;
        printf("file %s crc %x\n", filenameinzip, calculate_crc);
        return err;
}
//添加文件到压缩文件
void addFile(zipFile zf, const char* filenameinzip, char* name, char *password) {
    int size_buf = 0;
    void* buf = NULL;
    int err = 0;

    size_buf = WRITEBUFFERSIZE;
    buf = (void*)malloc(size_buf);
    if (buf == NULL)
    {
        printf("Error allocating memory\n");
        return ZIP_INTERNALERROR;
    }

    FILE * fin = NULL;
    int size_read;
    zip_fileinfo zi = { 0 };
    unsigned long crcFile = 0;
    //获取文件时间
    filetime(filenameinzip, &zi.dos_date);
    //获取文件crc效验值
    if ((password != NULL) && (err == ZIP_OK))
        err = getFileCrc(filenameinzip, buf, size_buf, &crcFile);

    //添加新的压缩文件
    err = zipOpenNewFileInZip3(zf, name, &zi,
        NULL, 0, NULL, 0, NULL,
        Z_DEFLATED, Z_DEFAULT_COMPRESSION, 0,
        -MAX_WBITS, DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
        password, crcFile);

    if (err != ZIP_OK)
        printf("error in opening %s in zipfile\n", filenameinzip);
    else
    {
        //打开文件
        fin = fopen(filenameinzip, "rb");
        if (fin == NULL)
        {
            err = ZIP_ERRNO;
            printf("error in opening %s for reading\n", filenameinzip);
        }
    }

    if (err == ZIP_OK) {
        do
        {
            err = ZIP_OK;
            //读入文件数据
            size_read = (int)fread(buf, 1, size_buf, fin);
            if (size_read < size_buf)
                if (feof(fin) == 0)
                {
                    printf("error in reading %s\n", filenameinzip);
                    err = ZIP_ERRNO;
                }

            if (size_read > 0)
            {
                //将数据写入目标文件
                err = zipWriteInFileInZip(zf, buf, size_read);
                if (err < 0)
                {
                    printf("error in writing %s in the zipfile\n",
                        filenameinzip);
                }

            }
            //循环写入
        } while ((err == ZIP_OK) && (size_read > 0));
    }

    //关闭文件
    if (fin)
        fclose(fin);

    if (err < 0)
        err = ZIP_ERRNO;
    else
    {
        //关闭输入文件
        err = zipCloseFileInZip(zf);
        if (err != ZIP_OK)
            printf("error in closing %s in the zipfile\n",
                filenameinzip);
    }
    free(buf);
}
BOOL addDir(zipFile zf, LPCSTR Path, char * root_path, char *password)
{
    WIN32_FIND_DATA FindData = {0};
    HANDLE hError;

    char FilePathName[1024];
    // 构造路径
    char FullPathName[1024];
    char RootPathName[1024];
    strcpy(FilePathName, Path);
    strcat(FilePathName, "\\*.*");
    hError = FindFirstFileA(FilePathName, &FindData);
    if (hError == INVALID_HANDLE_VALUE)
    {
        printf("搜索失败!");
        return 0;
    }
    while (FindNextFileA(hError, &FindData))
    {
        // 过虑.和..
        if (strcmp(FindData.cFileName, ".") == 0 || strcmp(FindData.cFileName, "..") == 0)
        {
            continue;
        }
        // 构造完整路径
        sprintf(FullPathName, "%s\\%s", Path, FindData.cFileName);
        sprintf(RootPathName, "%s\\%s", root_path, FindData.cFileName);
        // 输出本级的文件
        printf("%s,%s\n", FullPathName, RootPathName);
        if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            addDir(zf, FullPathName, RootPathName, password);
        }
        else
        {
            addFile(zf, FullPathName, RootPathName + 1, password);
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char* password = "aaaaa";
    char *variable = "aa";
    char *outfile = "tmp.h";
    char *comment = "";
    char *path = argv[1];

    for (size_t i = 2; i < argc - 1; i++)
    {
        if (strcmp(argv[i], "-o") == 0) {
            outfile = argv[i + 1];
            i++;
        }
        if (strcmp(argv[i], "-p") == 0) {
            password = argv[i + 1];
            i++;
        }
        if (strcmp(argv[i], "-c") == 0) {
            comment = argv[i + 1];
            i++;
        }
        if (strcmp(argv[i], "-v") == 0) {
            variable = argv[i + 1];
            i++;
        }
    }

    zipFile zf;
    int errclose;
    zlib_filefunc_def ffunc;
    fill_win32_filefunc(&ffunc);
    //打开压缩文件
    zf = zipOpen2("tmp.zip", 0, NULL, &ffunc);
    //添加文件夹到压缩文件9
    addDir(zf, path, "", password);
    //关闭压缩文件,输入压缩信息
    errclose = zipClose(zf, comment);
    if (errclose != ZIP_OK)
        printf("error in closing\n");

    //读取文件
    FILE *fin = fopen("tmp.zip", "rb");
    if (fin) {
        fseek(fin, 0, SEEK_END);
        size_t size = ftell(fin);
        fseek(fin, 0, SEEK_SET);
        if (size) {
            unsigned char *buf = malloc(size);
            if (size == fread(buf, 1, size, fin)) {
                //打开输出文件
                FILE *fout = fopen(outfile, "w");
                if (fout) {
                    fprintf(fout, "const char %s[]={ \r\n", variable);
                    unsigned char    ddl, ddh;
                    for (size_t i = 0, j = 0; i < size; i++, j++)
                    {
                        ddh = 48 + buf[i] / 16;
                        ddl = 48 + buf[i] % 16;
                        if (ddh > 57) ddh = ddh + 7;
                        if (ddl > 57) ddl = ddl + 7;
                        fprintf(fout, "0x%c%c", ddh, ddl);
                        if (i < size - 1) {
                            fprintf(fout, ", ");
                        }
                        if (j == 15) {
                            fprintf(fout, "\r\n");
                            j = 0;
                        }
                    }
                    fprintf(fout, "};");
                    fclose(fout);
                }
            }
            free(buf);
        }
        fclose(fin);
    }
    return 0;
}
