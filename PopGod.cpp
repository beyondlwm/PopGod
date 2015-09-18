#include "stdafx.h"
#include "Serializer.h"
#include "StringHelper.h"
#include "UtilityManager.h"
#include "iconv.h"
#include <algorithm>
#include <shlwapi.h>

struct STranslateRecord
{
    uint16_t m_uLength = 0;
    uint32_t m_uAddress = 0;
    std::string m_strOriginStr;
    std::string m_strProcessedStr;
};

std::string FilterControlCharacter(const std::string& strOri)
{
    std::string ret;
    bool bInControlChar = false;
    for (size_t i = 0; i < strOri.size(); ++i)
    {
        char character = strOri[i];
        if (character == '{' )
        {
            BEATS_ASSERT(!bInControlChar);
            bInControlChar = true;
        }
        else if (character == '}')
        {
            bInControlChar = false;
        }
        else if (!bInControlChar)
        {
            ret.append(&character, 1);
        }
    }
    return ret;
}

void ConvertString(iconv_t fd, const char* * inbuf, size_t *inbytesleft, char* * outbuf, size_t *outbytesleft, std::string& ret)
{
    size_t uInBytesLeft = *inbytesleft;
    char* pszoutBuffer = *outbuf;
    size_t uRet = iconv(fd, inbuf, inbytesleft, outbuf, outbytesleft);
    size_t convertSize = uInBytesLeft - *inbytesleft;
    if (convertSize > 0)
    {
        ret.append(pszoutBuffer, convertSize);
    }
    if (uRet == 0xFFFFFFFF)
    {
        ret.append("\x81\x48");
        if (*inbytesleft > 2)
        {
            uInBytesLeft = *inbytesleft - 2;
            (*inbuf) += 2;
            ConvertString(fd, inbuf, &uInBytesLeft, outbuf, outbytesleft, ret);
        }
    }
}

void ParseTextFile(iconv_t fd, const std::string& strFilePath, std::map<uint32_t, STranslateRecord>& recordMap)
{
    recordMap.clear();
    CSerializer sourceFile(strFilePath.c_str());
    SBufferData commaSymbo;
    commaSymbo.pData = ",";
    commaSymbo.dataLength = 1;

    SBufferData enterSymbo;
    enterSymbo.pData = "\r\n";
    enterSymbo.dataLength = 2;

    char szBuffer[10240];
    while (sourceFile.GetReadPos() != sourceFile.GetWritePos())
    {
        void* pLastReadPtr = sourceFile.GetReadPtr();
        uint32_t uLastReadPos = sourceFile.GetReadPos();
        uint32_t uSize = sourceFile.ReadToData(commaSymbo) - uLastReadPos;
        memcpy(szBuffer, pLastReadPtr, uSize);
        szBuffer[uSize] = 0;
        TCHAR* pEndChar = NULL;
        uint32_t uAddress = _tcstoul(szBuffer, &pEndChar, 16);
        BEATS_ASSERT(_tcslen(pEndChar) == 0, _T("Read uint from string %s error, stop at %s"), szBuffer, pEndChar);
        BEATS_ASSERT(errno == 0, _T("Call _tcstoul failed! string %s radix: 10"), szBuffer);
        BEATS_ASSERT(recordMap.find(uAddress) == recordMap.end());
        STranslateRecord* pNewRecord = &recordMap[uAddress];
        pNewRecord->m_uAddress = uAddress;
        sourceFile.SetReadPos(sourceFile.GetReadPos() + commaSymbo.dataLength);
        pLastReadPtr = sourceFile.GetReadPtr();
        uLastReadPos = sourceFile.GetReadPos();

        uSize = sourceFile.ReadToData(commaSymbo) - uLastReadPos;
        memcpy(szBuffer, pLastReadPtr, uSize);
        szBuffer[uSize] = 0;
        pEndChar = NULL;
        pNewRecord->m_uLength = (uint16_t)_tcstoul(szBuffer, &pEndChar, 10);
        BEATS_ASSERT(_tcslen(pEndChar) == 0, _T("Read uint from string %s error, stop at %s"), szBuffer, pEndChar);
        BEATS_ASSERT(errno == 0, _T("Call _tcstoul failed! string %s radix: 10"), szBuffer);
        sourceFile.SetReadPos(sourceFile.GetReadPos() + commaSymbo.dataLength);
        pLastReadPtr = sourceFile.GetReadPtr();
        uLastReadPos = sourceFile.GetReadPos();

        uSize = sourceFile.ReadToData(enterSymbo) - uLastReadPos;
        memcpy(szBuffer, pLastReadPtr, uSize);
        szBuffer[uSize] = 0;
        pNewRecord->m_strOriginStr = FilterControlCharacter(szBuffer);
        if (sourceFile.GetReadPos() != sourceFile.GetWritePos())
        {
            sourceFile.SetReadPos(sourceFile.GetReadPos() + enterSymbo.dataLength);
        }
        if (uSize > pNewRecord->m_uLength)
        {
            pNewRecord->m_strOriginStr.resize(pNewRecord->m_uLength);
            BEATS_WARNING(false, "%d length overflow, cut off some content!", pNewRecord->m_uAddress);
        }
        const char* pszReader = pNewRecord->m_strOriginStr.c_str();
        uint32_t uInputSize = pNewRecord->m_strOriginStr.length();
        ZeroMemory(szBuffer, 10240);
        char* pWriter = szBuffer;
        uint32_t uOutputSize = 10240;
        ConvertString(fd, &pszReader, &uInputSize, &pWriter, &uOutputSize, pNewRecord->m_strProcessedStr);
        BEATS_ASSERT(pNewRecord->m_strProcessedStr.length() <= pNewRecord->m_uLength);
        BEATS_PRINT("%p from %s to %s\n", pNewRecord->m_uAddress, pNewRecord->m_strOriginStr.c_str(), pNewRecord->m_strProcessedStr.c_str());
    }
}

void ConvertPalletDataFromGimToBMP(CSerializer& serializer, CSerializer& out)
{
    for (size_t i = 0; i < 1024;)
    {
        unsigned char R, G, B, A;
        serializer >> R >> G >> B >> A;
        if (A >= 0x80)
        {
            A = 0xFF;
        }
        else
        {
            A *= 2;
        }
        R = (unsigned char)(R * (float)A / 0xFF);
        G = (unsigned char)(G * (float)A / 0xFF);
        B = (unsigned char)(B * (float)A / 0xFF);
        out << B << G << R << A; //switch BGRA to RGBA.
        i += 4;
    }
}

void ExportPallet(CSerializer& serializer, const std::string& strOutputFileName)
{
    CSerializer palletData;
    int32_t uWidth = 256;
    int32_t uHeight = -256;
    BITMAPFILEHEADER header;
    memset(&header, 0, sizeof(header));
    header.bfType = 19778;
    header.bfOffBits = 1078;
    header.bfSize = header.bfOffBits + uWidth * uHeight;

    BITMAPINFOHEADER info;
    memset(&info, 0, sizeof(info));
    info.biBitCount = 8;
    info.biPlanes = 1;
    info.biWidth = uWidth;
    info.biHeight = uHeight;
    info.biSize = 40;
    info.biClrUsed = 256;
    info.biSizeImage = (info.biWidth * info.biBitCount + 31) / 32 * 4 * info.biHeight;

    palletData << header << info;

    ConvertPalletDataFromGimToBMP(serializer, palletData);
    for (int row = 0; row < 16; ++row)
    {
        for (int rowScan = 0; rowScan < 16; ++rowScan)
        {
            for (int col = 0; col < 16; ++col)
            {
                for (int k = 0; k < 16; ++k)
                {
                    BEATS_ASSERT(row * 16 + col <= 0xFF);
                    palletData << (unsigned char)(row * 16 + col);
                }
            }
        }
    }
    BEATS_ASSERT(palletData.GetWritePos() == 1078 + 256 * 256);
    palletData.Deserialize(strOutputFileName.c_str());
}

void ConvertTx2FileToBmp(CSerializer& tx2file, const std::string& outputFileName)
{
    short tx2Width, tx2Height;
    tx2file >> tx2Width >> tx2Height;
    short tx2bit;
    tx2file >> tx2bit;
    BEATS_ASSERT(tx2bit == 256);

    BITMAPFILEHEADER header;
    memset(&header, 0, sizeof(header));
    header.bfType = 19778;
    header.bfOffBits = 1078;
    header.bfSize = header.bfOffBits + tx2Height * tx2Width;

    BITMAPINFOHEADER info;
    memset(&info, 0, sizeof(info));
    info.biBitCount = 8;
    info.biPlanes = 1;
    info.biWidth = tx2Width;
    info.biHeight = -tx2Height;
    info.biSize = 40;
    info.biClrUsed = 256;
    info.biSizeImage = (info.biWidth * info.biBitCount + 31) / 32 * 4 * tx2Height;

    CSerializer bmpFile;
    bmpFile << header;
    bmpFile << info;

    //int txHeaderSize = tx2file.GetWritePos() - 1024 - tx2Height * tx2Width;
    //BEATS_ASSERT(txHeaderSize == 16);
    uint32_t uPosBeforePallet = tx2file.GetReadPos() + 10;
    tx2file.SetReadPos(uPosBeforePallet); // Skip tx2 header, we've already read another 6 bytes before.
    ExportPallet(tx2file, outputFileName + ".pallet.bmp");
    tx2file.SetReadPos(uPosBeforePallet); // Restore
    ConvertPalletDataFromGimToBMP(tx2file, bmpFile);
    bmpFile.Serialize(tx2file);
    bmpFile.SetReadPos(0);
    bmpFile.Deserialize(outputFileName.c_str());
}

void ConvertDataFileToBmp(const char* pszDataPath)
{
    std::string strDirectoryPath = pszDataPath;
    strDirectoryPath.append("_dir");
    CreateDirectory(strDirectoryPath.c_str(), nullptr);
    CSerializer datafile(pszDataPath, "rb");
    uint32_t uPalletOffset;
    datafile >> uPalletOffset;
    uint32_t uFileCount;
    datafile >> uFileCount;
    std::map<uint32_t, uint32_t> fileStruct;
    for (size_t i = 0; i < uFileCount; ++i)
    {
        uint32_t uUnknownData = 0;
        datafile >> uUnknownData;
        uint32_t txFileDataOffset = 0;
        datafile >> txFileDataOffset;
        BEATS_ASSERT(fileStruct.find(txFileDataOffset) == fileStruct.end());
        fileStruct[txFileDataOffset] = uUnknownData;
    }
    BEATS_ASSERT(datafile.GetReadPos() == uPalletOffset || uPalletOffset - datafile.GetReadPos() == 8); // Sometimes it needs align
    if (datafile.GetReadPos() != uPalletOffset)
    {
        datafile.SetReadPos(uPalletOffset);
    }
    for (auto iter = fileStruct.begin(); iter != fileStruct.end(); ++iter)
    {
        datafile.SetReadPos(iter->first);
        TCHAR szBuffer[256];
        _stprintf(szBuffer, "%s/%d.bmp", strDirectoryPath.c_str(),iter->second);
        ConvertTx2FileToBmp(datafile, szBuffer);
    }
}

void ConvertFontToBmp(CSerializer& fontFile, const std::string& outputFileName)
{
    int tx2Height = -24 * 80;
    int tx2Width = 24 * 30;
    BITMAPFILEHEADER header;
    memset(&header, 0, sizeof(header));
    header.bfType = 19778;
    header.bfOffBits = 1078;
    header.bfSize = header.bfOffBits + tx2Height * tx2Width;

    BITMAPINFOHEADER info;
    memset(&info, 0, sizeof(info));
    info.biBitCount = 8;
    info.biPlanes = 1;
    info.biWidth = tx2Width;
    info.biHeight = tx2Height;
    info.biSize = 40;
    info.biClrUsed = 256;
    info.biSizeImage = (info.biWidth * info.biBitCount + 31) / 32 * 4 * tx2Height;

    CSerializer bmpFile;
    bmpFile << header;
    bmpFile << info;
    fontFile.SetReadPos(16);
    ConvertPalletDataFromGimToBMP(fontFile, bmpFile);
    fontFile.SetReadPos(16);
    std::string palletFileName = outputFileName;
    palletFileName.append("_pallet.bmp");
    ExportPallet(fontFile, palletFileName);
    for (int row = 0; row < 80; ++row)// row for font character count
    {
        for (int ip = 0; ip < 24; ++ip)
        {
            for (int col = 0; col < 30; ++col) //col for font character count
            {
                uint32_t uOffset = (row * 30 + col) * 288 + ip * 12;
                fontFile.SetReadPos(uOffset + 0x410);
                for (int jp = 0; jp < 12; ++jp)
                {
                    unsigned char _4ppdata;
                    fontFile >> _4ppdata;
                    unsigned char high = _4ppdata >> 4;
                    unsigned char low = _4ppdata & 0x0F;
                    bmpFile << low << high;
                }
            }

        }
    }
    bmpFile.Deserialize(outputFileName.c_str());
}

void HandleDirectory(const SDirectory* directory)
{
    for (size_t i = 0; i < directory->m_pFileList->size(); ++i)
    {
        TFileData* pCurrFile = directory->m_pFileList->at(i);
        std::string extension = PathFindExtension(pCurrFile->cFileName);
        std::string filePath = directory->m_szPath;
        filePath.append("\\").append(pCurrFile->cFileName);
        if (_tcsicmp(extension.c_str(), ".tx2") == 0)
        {
            CSerializer tx2File(filePath.c_str());
            ConvertTx2FileToBmp(tx2File, filePath + ".bmp");
        }
        else if (extension == ".dat")
        {
            if (_tcsicmp(pCurrFile->cFileName, "start.dat") == 0 )
            {
                std::string directoryPath = directory->m_szPath;
                directoryPath.append("\\start.dat_dir");
                CreateDirectory(directoryPath.c_str(), nullptr);
                CSerializer startfile(filePath.c_str());
                uint32_t uFileCount = 0;
                startfile >> uFileCount;
                std::map<uint32_t, std::map<std::string, uint32_t>> fileLocationInfo;
                for (uint32_t i = 0; i < uFileCount; ++i)
                {
                    std::string strFileName;
                    startfile >> strFileName;
                    uint32_t ualignCount = startfile.GetReadPos() % 4;
                    if (ualignCount > 0)
                    {
                        startfile.SetReadPos(startfile.GetReadPos() + 4 - ualignCount);
                    }
                    uint32_t uDataAddress = 0;
                    while (true) // Sometimes it will align 4 more bytes, don't know why
                    {
                        startfile >> uDataAddress;
                        if (uDataAddress != 0)
                        {
                            break;
                        }
                    }
                    uint32_t uDataLength = 0;
                    startfile >> uDataLength;
                    std::map<std::string, uint32_t> filerecord;
                    filerecord[strFileName] = uDataLength;
                    fileLocationInfo[uDataAddress] = filerecord;
                }
                for (auto iter = fileLocationInfo.begin(); iter != fileLocationInfo.end(); ++iter)
                {
                    startfile.SetReadPos(iter->first);
                    std::string filePath = directoryPath;
                    filePath.append("\\").append(iter->second.begin()->first);
                    CSerializer newFile;
                    startfile.Deserialize(newFile, iter->second.begin()->second);
                    newFile.Deserialize(filePath.c_str());
                }
                SDirectory starDirectory(nullptr, directoryPath.c_str());
                CUtilityManager::GetInstance()->FillDirectory(starDirectory, true);
                HandleDirectory(&starDirectory);
            }
            else if (_tcsicmp(pCurrFile->cFileName, "upload00.dat") == 0)
            {
            }
            else if (_tcsicmp(pCurrFile->cFileName, "upload01.dat") == 0)
            {
            }
            else
            {
                ConvertDataFileToBmp(filePath.c_str());
            }
        }
    }
    for (size_t i = 0; i < directory->m_pDirectories->size(); ++i)
    {
        HandleDirectory(directory->m_pDirectories->at(i));
    }
}

void ExtractWholeProject(const std::string& strProjectPath)
{
    SDirectory projectDirectory(nullptr, strProjectPath.c_str());
    CUtilityManager::GetInstance()->FillDirectory(projectDirectory, true);
    HandleDirectory(&projectDirectory);
}

int _tmain(int argc, _TCHAR* argv[])
{
    iconv_t fd = iconv_open("SHIFT_JIS", "");
    if (fd != (iconv_t)0xFFFFFFFF)
    {
        std::map<uint32_t, STranslateRecord> recordMap;
        ParseTextFile(fd, "../Resource/SourceFile/bootdata.txt", recordMap);
        CSerializer bootDataFile("../Resource/SourceFile/boot.bin", "rb+");
        char* pBootData = (char*)bootDataFile.GetBuffer();
        for (auto iter = recordMap.begin(); iter != recordMap.end(); ++iter)
        {
            uint32_t uBaseAddress = iter->first;
            BEATS_ASSERT(iter->second.m_strProcessedStr.size() <= iter->second.m_uLength);
            for (size_t i = 0; i < iter->second.m_uLength; ++i)
            {
                if (i < iter->second.m_strProcessedStr.size())
                {
                    char data = iter->second.m_strProcessedStr[i];
                    pBootData[uBaseAddress + i] = data;
                }
                else
                {
                    pBootData[uBaseAddress + i] = 0; //If the translated text length is less than the orginal text, fill the rest with 0.
                }
            }
        }
        bootDataFile.Deserialize("../Resource/SourceFile/boot_hack.bin", "wb+");

        //ParseTextFile(fd, "../Resource/SourceFile/Chapter0.txt", recordMap);
        //CSerializer startDataFile("../Resource/SourceFile/start.DAT", "rb+");
        //char* pData = (char*)startDataFile.GetBuffer();
        //for (auto iter = recordMap.begin(); iter != recordMap.end(); ++iter)
        //{
        //    uint32_t uBaseAddress = 0x04aeb0 + iter->first;
        //    BEATS_ASSERT(iter->second.m_strProcessedStr.size() <= iter->second.m_uLength);
        //    for (size_t i = 0; i < iter->second.m_uLength; ++i)
        //    {
        //        if (i < iter->second.m_strProcessedStr.size())
        //        {
        //            char data = iter->second.m_strProcessedStr[i];
        //            pData[uBaseAddress + i] = ~data;
        //        }
        //        else
        //        {
        //            pData[uBaseAddress + i] = 0; //If the translated text length is less than the orginal text, fill the rest with 0.
        //        }
        //    }
        //}
        //startDataFile.Deserialize("../Resource/SourceFile/start_hack.DAT", "wb+");
        //iconv_close(fd);
    }
	return 0;
}

