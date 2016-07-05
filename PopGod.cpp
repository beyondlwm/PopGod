#include "stdafx.h"
#include "Serializer.h"
#include "StringHelper.h"
#include "UtilityManager.h"
#include "FilePathTool.h"
#include "iconv.h"
#include <algorithm>
#include <shlwapi.h>
#include <direct.h>
#include "ControlCode.h"

uint32_t uTotalFileCount = 0;
uint32_t uHandledFileCount = 0;
uint32_t uProcessProgress = 0;
iconv_t fd = 0;
iconv_t packfd = 0;
HWND BEYONDENGINE_HWND = nullptr;
TString strRootPath;
std::vector<TString> g_registeredSingleton;
std::map<unsigned short, TString> g_extractCodeMap;
std::map<TString, unsigned short> g_packetCodeMap;
struct STranslateRecord
{
    uint16_t m_uLength = 0;
    uint32_t m_uAddress = 0;
    std::string m_strOriginStr;
    std::string m_strProcessedStr;
};

void HandleDirectory(const SDirectory* directory);
void PackStartData(const std::string& originfilePath);

void LoadCodeMapData(const TString& strFilePath)
{
    g_extractCodeMap.clear();
    g_packetCodeMap.clear();
    CSerializer data(strFilePath.c_str(), "rb");
    data << (char)0;
    std::string strCache;
    data >> strCache;
    std::vector<std::string> codeMapItemList;
    std::vector<std::string> itemSpliter;
    CStringHelper::GetInstance()->SplitString(strCache.c_str(), "\r\n", codeMapItemList, true);

    for (size_t i = 0; i < codeMapItemList.size(); ++i)
    {
        if (!codeMapItemList[i].empty())
        {
            itemSpliter.clear();
            CStringHelper::GetInstance()->SplitString(codeMapItemList[i].c_str(), "=", itemSpliter, true);
            BEATS_ASSERT(itemSpliter.size() == 2);
            TCHAR* pEndChar = NULL;
            unsigned short uCode = (unsigned short)_tcstoul(itemSpliter[0].c_str(), &pEndChar, 16);
            uint32_t uCode2 = 0;
            for (size_t j = 0; j < itemSpliter[1].length(); ++ j)
            {
                uCode2 += (unsigned char)itemSpliter[1][j] << (j * 8);
            }
            char szBuffer[1024];
            std::wstring wstr = CStringHelper::GetInstance()->Utf8ToWString((const char*)&uCode2);
            BOOL bUseReplace = FALSE;
            WideCharToMultiByte(CP_ACP, 0, (LPWSTR)wstr.c_str(), -1, szBuffer, 1024, " ", &bUseReplace);
            std::string strValue = szBuffer;
            BEATS_ASSERT(g_extractCodeMap.find(uCode) == g_extractCodeMap.end());
            auto iter = g_packetCodeMap.find(strValue);
            if (bUseReplace || iter != g_packetCodeMap.end())
            {
                unsigned char* pszReader = (unsigned char*)&uCode;
                char szCodeBuffer[16];
                _stprintf(szCodeBuffer, "{[%.2x ", *(pszReader + 1));
                strValue = szCodeBuffer;
                _stprintf(szCodeBuffer, "%.2x]}", *pszReader);
                strValue.append(szCodeBuffer);
            }
            g_extractCodeMap[uCode] = strValue;
            iter = g_packetCodeMap.find(strValue);
            BEATS_ASSERT(iter == g_packetCodeMap.end());
            g_packetCodeMap[strValue] = uCode;
        }
    }
}

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

void ConvertBufferToString(iconv_t fd, CSerializer& serializerData, TString& strOut)
{
    while (serializerData.GetReadPos() < serializerData.GetWritePos() )
    {
        uint32_t uReadPos = serializerData.GetReadPos();
        unsigned char* pFirstData = (unsigned char*)serializerData.GetReadPtr();
        uint32_t uTryControlCode = *(uint32_t*)serializerData.GetReadPtr();
        uTryControlCode = BEYONDENGINE_SWAP32(uTryControlCode);
        if (*pFirstData == 0xFF)
        {
            unsigned char* pSecondData = pFirstData + 1;
            uint32_t uDataLength = *pSecondData;
            if ((uDataLength % 4) != 0)
            {
                uDataLength += (4 - uDataLength % 4);
            }
            serializerData.SetReadPos(serializerData.GetReadPos() + uDataLength);
            strOut.append("$");
        }
        else
        {
            CSerializer buffer;
            while (*pFirstData != 0xff && serializerData.GetReadPos() != serializerData.GetWritePos())
            {
                serializerData.Deserialize(buffer, 4);
                pFirstData = (unsigned char*)serializerData.GetReadPtr();
            }
            TString strRet;
            TString strUnknownCodeCache;
            for (size_t k = 0; k < buffer.GetWritePos();)
            {
                unsigned char* pszData = (unsigned char*)buffer.GetBuffer();
                if (pszData[k] == 0xdf)
                {
                    if (!strUnknownCodeCache.empty())
                    {
                        strUnknownCodeCache.append("]}");
                        strRet.append(strUnknownCodeCache);
                        strUnknownCodeCache.clear();
                    }
                    strRet.append(" ");
                    ++k;
                }
                else if (k + 1 != buffer.GetWritePos())
                {
                    unsigned short code = (unsigned char)pszData[k];
                    code = code << 8;
                    code += (unsigned char)pszData[k+1];
                    if (g_extractCodeMap.find(code) != g_extractCodeMap.end())
                    {
                        if (!strUnknownCodeCache.empty())
                        {
                            strUnknownCodeCache.append("]}");
                            strRet.append(strUnknownCodeCache);
                            strUnknownCodeCache.clear();
                        }
                        TString& strValueTmp = g_extractCodeMap[code];
                        strRet.append(strValueTmp);
                        k += 2;
                    }
                    else
                    {
                        if (!strUnknownCodeCache.empty())
                        {
                            strUnknownCodeCache.append(" ");
                        }
                        else
                        {
                            strUnknownCodeCache.append("{[");
                        }
                        TCHAR szBuffer[16];
                        _stprintf(szBuffer, "%.2x", (unsigned char)pszData[k]);
                        strUnknownCodeCache.append(szBuffer);
                        ++k;
                    }
                }
                else
                {
                    if (!strUnknownCodeCache.empty())
                    {
                        strUnknownCodeCache.append(" ");
                    }
                    else
                    {
                        strUnknownCodeCache.append("{[");
                    }
                    TCHAR szBuffer[16];
                    _stprintf(szBuffer, "%.2x", (unsigned char)pszData[k]);
                    strUnknownCodeCache.append(szBuffer);
                    ++k;
                }
            }
            if (!strUnknownCodeCache.empty())
            {
                strUnknownCodeCache.append("]}");
                strRet.append(strUnknownCodeCache);
                strUnknownCodeCache.clear();
            }
            strOut.append(strRet);
        }
    }
}

void ConvertString(iconv_t fd, const std::string& rawString, std::string& output, bool bWriteControlCode = false)
{
    const unsigned char* pData = (const unsigned char*)rawString.c_str();
    uint32_t uInputSize = rawString.length();
    TCHAR szBuffer[10240];
    uint32_t uWriteCounter = 0;
    while (uInputSize > 3)
    {
        if (pData[0] == 0xFF)
        {
            if (bWriteControlCode)
            {
                _stprintf(szBuffer, "0x%.2x%.2x%.2x%.2x ", pData[0], pData[1], pData[2], pData[3]);
                output.append(szBuffer);
            }
            pData += 4;
            uInputSize -= 4;
            uWriteCounter += 4;
        }
        else
        {
            unsigned char inputBuffer[4];
            inputBuffer[0] = ~pData[0];
            inputBuffer[1] = ~pData[1];
            inputBuffer[2] = ~pData[2];
            inputBuffer[3] = ~pData[3];
            unsigned char firstChar = inputBuffer[0];
            unsigned char secondChar = inputBuffer[1];
            bool bValid = ((firstChar >= 0x81 && firstChar <= 0x9F) || (firstChar >= 0xE0 && firstChar < 0xEF)) &&
                ((secondChar >= 0x40 && secondChar <= 0x7E) || (secondChar >= 0x80 && secondChar <= 0xFC));
            if (!bValid)
            {
                bValid = firstChar >= 0xF0 && firstChar <= 0xFC && ((secondChar >= 0x40 && secondChar <= 0x7E) || (secondChar >= 0x80 && secondChar <= 0xFC));
            }
            if (!bValid)
            {
                if (bWriteControlCode)
                {
                    _stprintf(szBuffer, "0x%.2x%.2x%.2x%.2x ", pData[0], pData[1], pData[2], pData[3]);
                    output.append(szBuffer);
                }
                pData += 4;
                uInputSize -= 4;
                uWriteCounter += 4;
            }
            else
            {
                const char* pReader = (const char*)inputBuffer;
                uint32_t uLeftBytes = uInputSize;
                char* pWriter = szBuffer;
                uint32_t uWriteLeft = 10240;
                iconv(fd, &pReader, &uLeftBytes, &pWriter, &uWriteLeft);
                uint32_t uWriteCount = uInputSize - uLeftBytes;
                if (uWriteCount == 0)
                {
                    if (bWriteControlCode)
                    {
                        _stprintf(szBuffer, "0x%.2x%.2x%.2x%.2x ", pData[0], pData[1], pData[2], pData[3]);
                        output.append(szBuffer);
                    }
                    pData += 4;
                    uInputSize -= 4;
                    uWriteCounter += 4;
                }
                else
                {
                    szBuffer[uWriteCount] = 0;
                    output.append(szBuffer);
                    if ((uWriteCount % 4) != 0)
                    {
                        uWriteCount = uWriteCount + 4 - (uWriteCount % 4);
                    }
                    pData += uWriteCount;
                    uInputSize -= uWriteCount;
                    uWriteCounter += uWriteCount;
                }
            }
        }
        if (uWriteCounter >= 32)
        {
            output.append("\r\n");
            uWriteCounter = 0;
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
        ConvertString(fd, pNewRecord->m_strOriginStr.c_str(), pNewRecord->m_strProcessedStr);
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
    int32_t uHeight = -256; // Inverse the image, use negative number.
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
    std::string strDirectoryPath = CStringHelper::GetInstance()->ReplaceString(pszDataPath, "Origin", "Decrypt");
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

void PackStoryData(const char* pszStoryDataFile)
{
    CSerializer storyFile(pszStoryDataFile);
    CSerializer packData;
    TString strTxtFilePath = CFilePathTool::GetInstance()->ParentPath(pszStoryDataFile);
    strTxtFilePath.append("/story.txt");
    if (!CFilePathTool::GetInstance()->Exists(strTxtFilePath.c_str()))
    {
        printf("无法找到%s！请执行第2步！", strTxtFilePath.c_str());
        return;
    }
    CSerializer textData(strTxtFilePath.c_str());
    textData << (char)0;
    std::vector<TString> lines;
    CStringHelper::GetInstance()->SplitString((const char*)textData.GetBuffer(), "\n", lines, false);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const TString& strValue = lines[i];
        const char* pszReader = strValue.c_str();
        char szAddr[16] = { 0 };
        memcpy(szAddr, pszReader, 10);
        TCHAR* pEndChar = NULL;
        uint32_t uValue = _tcstoul(szAddr, &pEndChar, 16);
        pszReader += 11;
        BEATS_ASSERT(uValue != 0 && uValue >storyFile.GetReadPos());
        storyFile.Deserialize(packData, uValue - storyFile.GetReadPos());
        BEATS_ASSERT(storyFile.GetReadPos() == uValue);
        bool bFileEnd = false;

        while (!bFileEnd && *pszReader != 0)
        {
            TString strCache;
            const char* pEndReader = strstr(pszReader, "$");
            if (pEndReader != nullptr)
            {
                uint32_t uCount = pEndReader - pszReader;
                strCache.append(pszReader, uCount);
                pszReader = pEndReader + 1;
            }
            else
            {
                strCache = pszReader;
                pszReader += strCache.length();
            }
            for (size_t j = 0; j < strCache.length();)
            {
                if (strCache[j] == 0x20)
                {
                    packData << (unsigned char)0xdf;
                    ++j;
                }
                else
                {
                    if (j + 1 != strCache.length())
                    {
                        if (strCache[j] == '{' && strCache[j + 1] == '[')
                        {
                            j += 2;
                            const char* pRawData = &strCache[j];
                            const char* pEndRawData = strstr(pRawData, "]}");
                            BEATS_ASSERT(pEndRawData != nullptr);
                            uint32_t uRawDataLength = (uint32_t)pEndRawData - (uint32_t)pRawData;
                            TString rawDataStr;
                            rawDataStr.append(pRawData, uRawDataLength);
                            std::vector<TString> rawDataList;
                            CStringHelper::GetInstance()->SplitString(rawDataStr.c_str(), " ", rawDataList, false);
                            for (size_t k = 0; k < rawDataList.size(); ++k)
                            {
                                TCHAR* pEndChar = NULL;
                                unsigned char uValue = (unsigned char)_tcstoul(rawDataList[k].c_str(), &pEndChar, 16);
                                BEATS_ASSERT(_tcslen(pEndChar) == 0);
                                packData << uValue;
                            }
                            j += uRawDataLength;
                            j += 2;//skip "]}"
                        }
                        else
                        {
                            char szBuffer[3];
                            szBuffer[0] = strCache[j];
                            szBuffer[1] = strCache[j + 1];
                            szBuffer[2] = 0;
                            auto iter = g_packetCodeMap.find(szBuffer);
                            BEATS_ASSERT(iter != g_packetCodeMap.end());
                            packData << (unsigned short)BEYONDENGINE_SWAP16(iter->second);
                            j += 2;
                        }
                    }
                    else
                    {
                        BEATS_ASSERT(false);
                    }
                }
            }
            while (*(unsigned char*)storyFile.GetReadPtr() != 0xFF)
            {
                if (storyFile.GetReadPos() == storyFile.GetWritePos())
                {
                    bFileEnd = true;
                    break;
                }
                else
                {
                    uint32_t uSkipData;
                    storyFile >> uSkipData;
                }
            }
            if (!bFileEnd && *(unsigned char*)storyFile.GetReadPtr() == 0xFF)
            {
                unsigned char* pSecondData = (unsigned char*)storyFile.GetReadPtr() + 1;
                uint32_t uDataLength = *pSecondData;
                if ((uDataLength % 4) != 0)
                {
                    uDataLength += (4 - uDataLength % 4);
                }
                storyFile.Deserialize(packData, uDataLength);
            }
        }
    }
    TString strDecryptPath = CFilePathTool::GetInstance()->ParentPath(pszStoryDataFile);
    strDecryptPath.append("/NewStory.dat");
    packData.Deserialize(strDecryptPath.c_str());
}

void ExtractStoryData(const char* pszStoryDataFile)
{
    CSerializer storyFile(pszStoryDataFile);
    uint32_t uCode = 0;
    SBufferData startData;
    uint32_t uStartCode = BEYONDENGINE_SWAP32(0xFF0A0300);
    startData.pData = &uStartCode;
    startData.dataLength = 4;

    SBufferData endData;
    uint32_t uEndCode = BEYONDENGINE_SWAP32(0xFF040400);
    endData.pData = &uEndCode;
    endData.dataLength = 4;
    CSerializer text;
    uint32_t uEndPos = 0;
    while (storyFile.GetReadPos() < storyFile.GetWritePos())
    {
        uint32_t uStartPos = storyFile.ReadToData(startData, false);
        if (uStartPos == storyFile.GetWritePos())
        {
            if (uEndPos != storyFile.GetWritePos())
            {
                storyFile.SetReadPos(uEndPos);
                CSerializer inputData;
                storyFile.Deserialize(inputData);
                std::string outStr;
                ConvertBufferToString(fd, inputData, outStr);
                TCHAR szLocBuffer[1024];
                _stprintf(szLocBuffer, "0x%p %s", uStartPos, outStr.c_str());
                text << szLocBuffer;
                text.SetWritePos(text.GetWritePos() - 1);
            }
            break;
        }
        uStartPos += 12;
        storyFile.SetReadPos(uStartPos);
        uEndPos = storyFile.ReadToData(endData, false);
        if (uEndPos > uStartPos)
        {
            CSerializer inputData;
            inputData.Serialize(storyFile.GetBuffer() + uStartPos, uEndPos - uStartPos);
            std::string outStr;
            ConvertBufferToString(fd, inputData, outStr);
            TCHAR szLocBuffer[1024];
            _stprintf(szLocBuffer, "0x%p %s\n", uStartPos, outStr.c_str());
            text << szLocBuffer;
            text.SetWritePos(text.GetWritePos() - 1);
        }
    }
    TString strParentPath = CFilePathTool::GetInstance()->ParentPath(pszStoryDataFile);
    strParentPath.append("/story.txt");
    text.Deserialize(strParentPath.c_str(), "wb+");
}

void ExtractStartData(const std::string& filePath, const std::string& decryptPath)
{
    CFilePathTool::GetInstance()->MakeDirectory(decryptPath.c_str());
    CSerializer startfile(filePath.c_str());
    uint32_t uFileCount = 0;
    startfile >> uFileCount;
    std::map<uint32_t, std::map<std::string, uint32_t>> fileLocationInfo;
    for (uint32_t i = 0; i < uFileCount; ++i)
    {
        char szFileName[16] = {0};
        startfile.Deserialize(szFileName, 16);
        uint32_t uDataAddress = 0;
        startfile >> uDataAddress;
        uint32_t uDataLength = 0;
        startfile >> uDataLength;
        std::map<std::string, uint32_t> filerecord;
        filerecord[szFileName] = uDataLength;
        fileLocationInfo[uDataAddress] = filerecord;
    }
    for (auto iter = fileLocationInfo.begin(); iter != fileLocationInfo.end(); ++iter)
    {
        startfile.SetReadPos(iter->first);
        std::string filePath = decryptPath;
        filePath.append("/").append(iter->second.begin()->first);
        CSerializer newFile;
        startfile.Deserialize(newFile, iter->second.begin()->second);
        newFile.Deserialize(filePath.c_str());
    }
}

void PackStartData(const std::string& originfilePath)
{
    CSerializer originData(originfilePath.c_str());
    CSerializer ret;
    uint32_t uFileCount = 0;
    originData >> uFileCount;
    ret << uFileCount;
    std::map<uint32_t, std::map<std::string, uint32_t>> fileLocationInfo;
    std::map<TString, uint32_t> revertMap;//because we may change the file size, so all the address and data size may change after we serialize.
    for (uint32_t i = 0; i < uFileCount; ++i)
    {
        char szFileName[16] = { 0 };
        originData.Deserialize(szFileName, 16);
        ret.Serialize(szFileName, 16);
        uint32_t uDataAddress = 0;
        originData >> uDataAddress;
        ret << uDataAddress;
        uint32_t uDataLength = 0;
        originData >> uDataLength;
        ret << uDataLength;
        revertMap[szFileName] = i;
        std::map<std::string, uint32_t> filerecord;
        filerecord[szFileName] = uDataLength;
        fileLocationInfo[uDataAddress] = filerecord;
    }
    ret << 0 << 0 << 0;//padding 12 bytes 0.
    TString decryptPath = CStringHelper::GetInstance()->ReplaceString(originfilePath, "PopGod", "Decrypt");
    decryptPath = CFilePathTool::GetInstance()->ParentPath(decryptPath.c_str());
    decryptPath.append("/start.dat_dir");
    if (!CFilePathTool::GetInstance()->IsDirectory(decryptPath.c_str()))
    {
        printf("无法找到%s文件夹，请先执行第1步！", decryptPath.c_str());
        return;
    }
    for (auto iter = fileLocationInfo.begin(); iter != fileLocationInfo.end(); ++iter)
    {
        TString strName = iter->second.begin()->first;
        BEATS_ASSERT(revertMap.find(strName) != revertMap.end());
        std::string filePath = decryptPath;
        filePath.append("/").append(strName);
        uint32_t uNewPos = ret.GetWritePos();
        if (_tcsicmp(strName.c_str(), "story.dat") == 0)
        {
            TString newstoryfilePath = decryptPath;
            newstoryfilePath.append("/newstory.dat");
            if (CFilePathTool::GetInstance()->Exists(newstoryfilePath.c_str()))
            {
                filePath = newstoryfilePath;
            }
        }
        ret.Serialize(filePath.c_str(), "rb");
        uint32_t uNewSize = ret.GetWritePos() - uNewPos;
        uint32_t uRecordWritePos = ret.GetWritePos();
        uint32_t uIndex = revertMap[strName];
        uint32_t uAddrOffset = 4 + (uIndex + 1) * 16 + uIndex * 8;
        ret.SetWritePos(uAddrOffset);
        uint32_t* pAddr = (uint32_t*)ret.GetWritePtr();
        *pAddr = uNewPos; //update the new address
        ++pAddr;
        *pAddr = uNewSize;//update the new size
        ret.SetWritePos(uRecordWritePos);
    }
    TString output = CFilePathTool::GetInstance()->ParentPath(strRootPath.c_str());
    output.append("/newstart.dat");
    ret.Deserialize(output.c_str());
}

void HandleDirectory(const SDirectory* directory)
{
    std::string decryptPath = CStringHelper::GetInstance()->ReplaceString(directory->m_szPath, "PopGod", "Decrypt");
    CFilePathTool::GetInstance()->MakeDirectory(decryptPath.c_str());
    for (size_t i = 0; i < directory->m_pFileList->size(); ++i)
    {
        TFileData* pCurrFile = directory->m_pFileList->at(i);
        std::string extension = PathFindExtension(pCurrFile->cFileName);
        std::string filePath = directory->m_szPath;
        filePath.append(pCurrFile->cFileName);
        if (_tcsicmp(extension.c_str(), ".tx2") == 0)
        {
            CSerializer tx2File(filePath.c_str());
            std::string decryptPath = CStringHelper::GetInstance()->ReplaceString(filePath, "PopGod", "Decrypt");
            decryptPath.append(".bmp");
            ConvertTx2FileToBmp(tx2File, decryptPath);
        }
        else if (_tcsicmp(extension.c_str(), ".dat") == 0)
        {
            if (_tcsicmp(pCurrFile->cFileName, "start.dat") == 0)
            {
                ExtractStartData(filePath, decryptPath);
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
                ExtractStoryData(filePath.c_str());
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
            std::string strDecryptFile = decryptPath;
            strDecryptFile.append(pCurrFile->cFileName);
            strDecryptFile.append(".bmp");
            ConvertFontToBmp(fontFile, strDecryptFile);
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
        uint32_t curProgress = 100;
        if (uTotalFileCount > 0)
        {
            curProgress = uHandledFileCount * 100 / uTotalFileCount;
            if (curProgress > 100)
            {
                curProgress = 100;
            }
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
    printf("                    流行之神1汉化破解程序\n\n\
请将ISO镜像解压到命名为\"PopGod\"的文件夹，并运行本程序于同级目录\n\
请将码表命名为\"CodeMap.txt\"，并放置于本程序运行目录\n\
0. 输入0开始解压整个项目\n\
1. 输入1开始解压start.dat\n\
2. 输入2开始转换story.dat为story.txt\n\
3. 输入3开始还原start.dat\n");
    int a = getchar();
    TCHAR szBuffer[MAX_PATH];
    GetCurrentDirectory(MAX_PATH, szBuffer);
    //TString strRootPath = szBuffer;
    strRootPath = "D:/PSP_crack/psp/PopGod";
    bool bFindDirectory = CFilePathTool::GetInstance()->IsDirectory(strRootPath.c_str());
    if (bFindDirectory)
    {
        switch (a)
        {
        case '0':
        {
                    TString strWorkPath = strRootPath;
                    ExtractWholeProject(strRootPath.c_str());
                    printf("解包完成，按任意键退出\n");
                    system("pause");
        }
            break;
        case '1':
        {
                    TString strStartDataPath = strRootPath + "/PSP_GAME/USRDIR/start.dat";
                    TString strDecryptPath = CStringHelper::GetInstance()->ReplaceString(strStartDataPath, "PopGod", "Decrypt");
                    strDecryptPath.append("_dir/");
                    ExtractStartData(strStartDataPath.c_str(), strDecryptPath.c_str());
                    printf("已解压到%s\n按任意键退出\n", strDecryptPath.c_str());
                    system("pause");
        }
            break;
        case '2':
        {
                    TString strCodeMapFilePath = CFilePathTool::GetInstance()->ParentPath(strRootPath.c_str());
                    strCodeMapFilePath.append("/CodeMap.txt");
                    LoadCodeMapData(strCodeMapFilePath.c_str());
                    TString storyFile = CStringHelper::GetInstance()->ReplaceString(strRootPath, "PopGod", "Decrypt");
                    storyFile.append("/PSP_GAME/USRDIR/start.dat_dir/story.dat");
                      ExtractStoryData(storyFile.c_str());
                      printf("已解压%s为story.txt\n按任意键退出\n", storyFile.c_str());
                      system("pause");
        }
            break;
        case '3':
        {
                    TString strCodeMapFilePath = CFilePathTool::GetInstance()->ParentPath(strRootPath.c_str());
                    strCodeMapFilePath.append("/CodeMap.txt");
                    LoadCodeMapData(strCodeMapFilePath.c_str());
                    TString storyFile = CStringHelper::GetInstance()->ReplaceString(strRootPath, "PopGod", "Decrypt");
                    storyFile.append("/PSP_GAME/USRDIR/start.dat_dir/story.dat");
                    PackStoryData(storyFile.c_str());
                    TString strStartFilePath = strRootPath;
                    strStartFilePath.append("/PSP_GAME/USRDIR/start.dat");
                    PackStartData(strStartFilePath.c_str());
                    TString output = CFilePathTool::GetInstance()->ParentPath(strRootPath.c_str());
                    output.append("/newstart.dat");
                    printf("已经打包为%s\n按任意键退出", output.c_str());
                    system("pause");
        }
            break;
        default:
            break;
        }
    }
    else
    {
        _stprintf(szBuffer, "未发现文件夹%s\n按任意键退出\n", strRootPath.c_str());
        printf("szBuffer");
        system("pause");
    }
    return 0;
    fd = iconv_open("", "SHIFT_JIS");
    packfd = iconv_open("SHIFT_JIS", "");
    if (fd != (iconv_t)0xFFFFFFFF && packfd != (iconv_t)0xFFFFFFFF)
    {
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

