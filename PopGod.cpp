#include "stdafx.h"
#include "Serializer.h"
#include "StringHelper.h"
#include "UtilityManager.h"
#include "iconv.h"
#include <algorithm>
#include <shlwapi.h>

uint32_t uTotalFileCount = 0;
uint32_t uHandledFileCount = 0;
uint32_t uProcessProgress = 0;
iconv_t fd = 0;
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

void ConvertString(iconv_t fd, const std::string& rawString, std::string& output)
{
    const char* pszReader = rawString.c_str();
    uint32_t uInputSize = rawString.length();
    char szBuffer[10240];
    ZeroMemory(szBuffer, 10240);
    char* pWriter = szBuffer;
    uint32_t uOutputSize = 10240;
    ConvertString(fd, &pszReader, &uInputSize, &pWriter, &uOutputSize, output);
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

void DecryptPalette(CSerializer& serializer, CSerializer& out)
{
    uint32_t uOriginOutWritePos = out.GetWritePos();
    serializer.Deserialize(out, 0x20); // First 0x20 bytes no need to transfer.
    bool bNeedTransfer = true;
    while (out.GetWritePos() - uOriginOutWritePos < 0x400)
    {
        if (bNeedTransfer)
        {
            unsigned char last32Bytes[32];
            serializer.Deserialize(last32Bytes, 32);
            serializer.Deserialize(out, 32);
            out.Serialize(last32Bytes, 32);
            bNeedTransfer = false;
        }
        else
        {
            uint32_t uRestDataLength = serializer.GetWritePos() - serializer.GetReadPos();
            if (uRestDataLength >= 0x40)
            {
                uRestDataLength = 0x40;
            }
            serializer.Deserialize(out, uRestDataLength);
            bNeedTransfer = true;
        }
    }
}

void ConvertPaletteDataFromGimToBMP(CSerializer& serializer, CSerializer& out, bool bNeedDecrypt)
{
    CSerializer paletteData;
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
        paletteData << B << G << R << A; //switch BGRA to RGBA.
        i += 4;
    }
    if (bNeedDecrypt)
    {
        CSerializer tmpDecrypt;
        DecryptPalette(paletteData, tmpDecrypt);
        paletteData.Reset();
        tmpDecrypt.SetReadPos(0);
        tmpDecrypt.Deserialize(paletteData);
        BEATS_ASSERT(paletteData.GetWritePos() == 0x400);
    }
    out.Serialize(paletteData);
}

void ExportPalette(CSerializer& serializer, const std::string& strOutputFileName, bool bNeedDecrypt)
{
    CSerializer paletteData;
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

    paletteData << header << info;
    CSerializer gimPalette;
    ConvertPaletteDataFromGimToBMP(serializer, gimPalette, bNeedDecrypt);
    paletteData.Serialize(gimPalette);
    for (int row = 0; row < 16; ++row)
    {
        for (int rowScan = 0; rowScan < 16; ++rowScan)
        {
            for (int col = 0; col < 16; ++col)
            {
                for (int k = 0; k < 16; ++k)
                {
                    BEATS_ASSERT(row * 16 + col <= 0xFF);
                    paletteData << (unsigned char)(row * 16 + col);
                }
            }
        }
    }
    BEATS_ASSERT(paletteData.GetWritePos() == 1078 + 256 * 256);
    paletteData.Deserialize(strOutputFileName.c_str());
}

void ConvertTx2FileToBmp(CSerializer& tx2file, const std::string& outputFileName)
{
    uint32_t uOrignalReadPos = tx2file.GetReadPos();
    short tx2Width, tx2Height;
    tx2file >> tx2Width >> tx2Height;
    short tx2bit;
    tx2file >> tx2bit;
    tx2file.SetReadPos(0xF);
    unsigned char decryptFlag;
    tx2file >> decryptFlag;
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
    tx2file.SetReadPos(uOrignalReadPos + 16);
    ExportPalette(tx2file, outputFileName + ".pallet.bmp", decryptFlag == 0);
    tx2file.SetReadPos(uOrignalReadPos + 16); // Restore
    ConvertPaletteDataFromGimToBMP(tx2file, bmpFile, decryptFlag == 0);
    bmpFile.Serialize(tx2file);
    bmpFile.SetReadPos(0);
    bmpFile.Deserialize(outputFileName.c_str());
}

void ExtractDataFileToBmp(const char* pszDataPath)
{
    std::string strDirectoryPath = pszDataPath;
    strDirectoryPath.append("_dir");
    CreateDirectory(strDirectoryPath.c_str(), nullptr);
    CSerializer datafile(pszDataPath, "rb");
    uint32_t uPaletteOffset;
    datafile >> uPaletteOffset;
    uint32_t uFileCount;
    datafile >> uFileCount;
    std::map<uint32_t, uint32_t> fileStruct;
    for (size_t i = 0; i < uFileCount; ++i)
    {
        uint32_t uUnknownData = 0;
        datafile >> uUnknownData;
        uint32_t txFileDataOffset = 0;
        datafile >> txFileDataOffset;
        BEATS_ASSERT(fileStruct.find(txFileDataOffset) == fileStruct.end(), "find overlap file offset in %s, offset = 0x%p", pszDataPath, txFileDataOffset);
        fileStruct[txFileDataOffset] = uUnknownData;
    }
    BEATS_ASSERT(datafile.GetReadPos() == uPaletteOffset || uPaletteOffset - datafile.GetReadPos() == 8); // Sometimes it needs align
    if (datafile.GetReadPos() != uPaletteOffset)
    {
        datafile.SetReadPos(uPaletteOffset);
    }
    uint32_t uFileCounter = 0;
    for (auto iter = fileStruct.begin(); iter != fileStruct.end(); ++iter)
    {
        datafile.SetReadPos(iter->first);
        uint32_t uFileLength = datafile.GetWritePos() - iter->first;
        if (uFileCounter != fileStruct.size() - 1)
        {
            auto nextIter = iter;
            ++nextIter;
            uFileLength = nextIter->first - iter->first;
        }
        CSerializer txFileSerializer;
        datafile.Deserialize(txFileSerializer, uFileLength);
        TCHAR szBuffer[256];
        _stprintf(szBuffer, "%s/%d.tx2", strDirectoryPath.c_str(), iter->second);
        txFileSerializer.Deserialize(szBuffer);
        txFileSerializer.SetReadPos(0);

        _stprintf(szBuffer, "%s/%d.bmp", strDirectoryPath.c_str(),iter->second);
        ConvertTx2FileToBmp(txFileSerializer, szBuffer);
        ++uFileCounter;
    }
}

void ConvertFontToBmp(CSerializer& fontFile, const std::string& outputFileName)
{
    int32_t uCharacterWidth = 0;
    int32_t uCharacterHeight = 0;
    fontFile >> uCharacterWidth >> uCharacterHeight;
    int32_t uBytesForCharacterRow = uCharacterWidth / 2;
    int32_t uCharacterBytes = uBytesForCharacterRow * uCharacterHeight;
    uint32_t uFontFileSize = fontFile.GetWritePos();
    uFontFileSize -= 16;
    uFontFileSize -= 1024;
    uint32_t uUselessBytesLength = uFontFileSize % uCharacterBytes;
    if (uUselessBytesLength != 0)
    {
        uFontFileSize -= uUselessBytesLength;
    }
    BEATS_ASSERT(uFontFileSize % uCharacterBytes == 0);
    size_t uCharacterCount = uFontFileSize / uCharacterBytes;

    static const int32_t uCharacterCol = 60;
    int32_t uRowCount = uCharacterCount / uCharacterCol;
    if (uCharacterCount % uCharacterCol > 0)
    {
        ++uRowCount;
    }
    int32_t tx2Height = -uCharacterHeight * uRowCount;
    int32_t tx2Width = uCharacterWidth * uCharacterCol;
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
    ConvertPaletteDataFromGimToBMP(fontFile, bmpFile, false);
    fontFile.SetReadPos(16);
    std::string palletFileName = outputFileName;
    palletFileName.append("_pallet.bmp");
    ExportPalette(fontFile, palletFileName, false);
    for (int row = 0; row < uRowCount; ++row)// row for font character count
    {
        for (int ip = 0; ip < uCharacterHeight; ++ip)
        {
            for (int col = 0; col < uCharacterCol; ++col) //col for font character count
            {
                uint32_t uOffset = (row * uCharacterCol + col) * uCharacterBytes + ip * uBytesForCharacterRow;
                if (uOffset + 0x410 >= uFontFileSize)
                {
                    for (int jp = 0; jp < uBytesForCharacterRow; ++jp)
                    {
                        unsigned char fillchar = 0;
                        bmpFile << fillchar << fillchar;
                    }
                }
                else
                {
                    fontFile.SetReadPos(uOffset + 0x410);
                    for (int jp = 0; jp < uBytesForCharacterRow; ++jp)
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
        filePath.append(pCurrFile->cFileName);
        if (_tcsicmp(extension.c_str(), ".tx2") == 0)
        {
            CSerializer tx2File(filePath.c_str());
            ConvertTx2FileToBmp(tx2File, filePath + ".bmp");
        }
        else if (_tcsicmp(extension.c_str(), ".dat") == 0)
        {
            if (_tcsicmp(pCurrFile->cFileName, "start.dat") == 0)
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
            else if (_tcsicmp(pCurrFile->cFileName, "data.dat") == 0)
            {
                // Record the info of file length and address from somewhere, could ignore.
                std::vector<std::string> nameList;
                CSerializer data(filePath.c_str());
                uint32_t uFileCount = 0;
                data >> uFileCount;
                std::string strName;
                for (size_t i = 0; i < uFileCount; ++i)
                {
                    data >> strName;
                    uint32_t uAlign = data.GetReadPos() % 4;
                    if (uAlign != 0)
                    {
                        data.SetReadPos(data.GetReadPos() + 4 - uAlign);
                        BEATS_ASSERT(data.GetReadPos() % 4 == 0);
                    }
                    uint32_t uAddress, uLength;
                    data >> uAddress;
                    if (uAddress == 0)
                    {
                        data >> uAddress;
                    }
                    data >> uLength;
                    BEATS_PRINT("%s 0x%p 0x%p\n", strName.c_str(), uAddress, uLength);
                }
            }
            else if (_tcsicmp(pCurrFile->cFileName, "keyword.dat") == 0)
            {
            }
            else if (_tcsicmp(pCurrFile->cFileName, "OCCULTFILE.DAT") == 0)
            {
            }
            else if (_tcsicmp(pCurrFile->cFileName, "STORY.DAT") == 0)
            {
            }
            else if (_tcsicmp(pCurrFile->cFileName, "LOGIC.DAT") == 0)
            {

            }
            else if (_tcsicmp(pCurrFile->cFileName, "SELECTER.DAT") == 0)
            {
                CSerializer selectorFile(filePath.c_str());
                uint32_t uSelectCount = 0;
                selectorFile >> uSelectCount;
                std::map<uint32_t, uint32_t> info;
                uint32_t uLastAddress = 0;
                for (size_t i = 0; i < uSelectCount; ++i)
                {
                    uint32_t uAddress = 0;
                    selectorFile >> uAddress;
                    if (uLastAddress > 0)
                    {
                        info[uLastAddress] = uAddress - uLastAddress - 32 - 16;
                    }
                    uLastAddress = uAddress;
                }
                info[uLastAddress] = selectorFile.GetWritePos() - uLastAddress - 32 - 16;
                for (auto iter = info.begin(); iter != info.end(); ++iter)
                {
                    uint32_t uStarAddress = iter->first;
                    selectorFile.SetReadPos(uStarAddress);
                    std::string strTitle;
                    selectorFile >> strTitle;
                    selectorFile.SetReadPos(uStarAddress + 32);//maybe it is a char[32].
                    std::string strChapterInfo;
                    selectorFile >> strChapterInfo; // maybe it is a char[16].
                    selectorFile.SetReadPos(uStarAddress + 32 + 16);
                    uint32_t uLength = iter->second;
                    BEATS_ASSERT(uLength % 4 == 0);
                    for (size_t i = 0; i < strTitle.size(); ++i)
                    {
                        strTitle[i] = ~strTitle[i];
                    }
                    for (size_t i = 0; i < strChapterInfo.size(); ++i)
                    {
                        strChapterInfo[i] = ~strChapterInfo[i];
                    }
                    std::string strTitle2, strChapterInfo2;
                    ConvertString(fd, strTitle, strTitle2);
                    ConvertString(fd, strChapterInfo, strChapterInfo2);
                    BEATS_PRINT("%s %s\n", strTitle2.c_str(), strChapterInfo2.c_str());
                    while (uLength > 0)
                    {
                        uint32_t uData;
                        selectorFile >> uData;
                        BEATS_PRINT(" 0x%p", uData);
                        uLength -= 4;
                    }
                    BEATS_PRINT("\n");
                }
            }
            else
            {
                ExtractDataFileToBmp(filePath.c_str());
            }
        }
        else if (_tcsicmp(extension.c_str(), ".ftx") == 0)
        {
            CSerializer fontFile(filePath.c_str());
            ConvertFontToBmp(fontFile, filePath + ".bmp");
        }
        else if (_tcsicmp(pCurrFile->cFileName, "logic_dispos.pak") == 0) // This file is related to the game logic.
        {
            CSerializer logic_disposFile(filePath.c_str());
            uint32_t postCount = 0;
            logic_disposFile >> postCount;
            std::map<uint32_t, uint32_t> chapterInfo;
            for (size_t i = 0; i < postCount; ++i)
            {
                uint32_t uStartPos, uLength;
                logic_disposFile >> uStartPos >> uLength;
                BEATS_ASSERT(chapterInfo.find(uStartPos) == chapterInfo.end());
                chapterInfo[uStartPos] = uLength;
            }
            for (auto iter = chapterInfo.begin(); iter != chapterInfo.end(); ++iter)
            {
                logic_disposFile.SetReadPos(iter->first);
                BEATS_PRINT("addr: 0x%p len:%d\n", iter->first, iter->second);
                while (true)
                {
                    uint32_t uHeader, a, b, pictureNameId, d, e;
                    logic_disposFile >> uHeader;
                    if (uHeader == 0x5F444e45) //"END_" flag
                    {
                        break;
                    }
                    logic_disposFile >> a >> b >> pictureNameId >> d >> e;
                    BEATS_PRINT("header:%d data: %d %d %d %d %d\n", uHeader, a, b, pictureNameId, d, e); // if pictureNameId = 6108, we must got the file 6108.tx2
                }
            }
        }
        ++uHandledFileCount;
        uint32_t curProgress = uHandledFileCount * 100 / uTotalFileCount;
        if (curProgress > 100)
        {
            curProgress = 100;
        }
        system("cls");
        printf("当前进度：%d%%    请稍等。。。", curProgress);
        printf("正在处理：%s", pCurrFile->cFileName);
    }
    for (size_t i = 0; i < directory->m_pDirectories->size(); ++i)
    {
        HandleDirectory(directory->m_pDirectories->at(i));
    }
}

uint32_t GetFileCount(const SDirectory* pDirectory)
{
    uint32_t uCount = pDirectory->m_pFileList->size();
    for (size_t i = 0; i < pDirectory->m_pDirectories->size(); ++i)
    {
        uCount += GetFileCount(pDirectory->m_pDirectories->at(i));
    }
    return uCount;
}

void ExtractWholeProject(const std::string& strProjectPath)
{
    SDirectory projectDirectory(nullptr, strProjectPath.c_str());
    CUtilityManager::GetInstance()->FillDirectory(projectDirectory, true);
    uTotalFileCount = GetFileCount(&projectDirectory);
    HandleDirectory(&projectDirectory);
}

int _tmain(int argc, _TCHAR* argv[])
{
    fd = iconv_open("", "SHIFT_JIS");
    if (fd != (iconv_t)0xFFFFFFFF)
    {
        TCHAR szBuffer[MAX_PATH];
        GetCurrentDirectory(MAX_PATH, szBuffer);
        ExtractWholeProject("D:\\PSP_crack\\psp\\Hayarigami_Portable_Keishichou_Kaii_Jiken_JNP");
        printf("解包完成。");
        system("pause");

    //    std::map<uint32_t, STranslateRecord> recordMap;
    //    ParseTextFile(fd, "../Resource/SourceFile/bootdata.txt", recordMap);
    //    CSerializer bootDataFile("../Resource/SourceFile/boot.bin", "rb+");
    //    char* pBootData = (char*)bootDataFile.GetBuffer();
    //    for (auto iter = recordMap.begin(); iter != recordMap.end(); ++iter)
    //    {
    //        uint32_t uBaseAddress = iter->first;
    //        BEATS_ASSERT(iter->second.m_strProcessedStr.size() <= iter->second.m_uLength);
    //        for (size_t i = 0; i < iter->second.m_uLength; ++i)
    //        {
    //            if (i < iter->second.m_strProcessedStr.size())
    //            {
    //                char data = iter->second.m_strProcessedStr[i];
    //                pBootData[uBaseAddress + i] = data;
    //            }
    //            else
    //            {
    //                pBootData[uBaseAddress + i] = 0; //If the translated text length is less than the orginal text, fill the rest with 0.
    //            }
    //        }
    //    }
    //    bootDataFile.Deserialize("../Resource/SourceFile/boot_hack.bin", "wb+");

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
        iconv_close(fd);
    }
    return 0;
}

