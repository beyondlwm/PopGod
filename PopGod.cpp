#include "stdafx.h"
#include "Serializer.h"
#include "StringHelper.h"
#include "iconv.h"
#include <algorithm>

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

int _tmain(int argc, _TCHAR* argv[])
{
    iconv_t fd = iconv_open("SHIFT_JIS", "");
    if (fd != (iconv_t)0xFFFFFFFF)
    {
        CSerializer sourceFile("../Resource/SourceFile/Chapter0.txt");
        SBufferData commaSymbo;
        commaSymbo.pData = ",";
        commaSymbo.dataLength = 1;
        
        SBufferData enterSymbo;
        enterSymbo.pData = "\r\n";
        enterSymbo.dataLength = 2;

        std::map<uint32_t, STranslateRecord> recordMap;
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
        CSerializer startDataFile("../Resource/SourceFile/start.DAT", "rb+");
        char* pData = (char*)startDataFile.GetBuffer();
        for (auto iter = recordMap.begin(); iter != recordMap.end(); ++iter)
        {
            uint32_t uBaseAddress = 0x04aeb0 + iter->first;
            BEATS_ASSERT(iter->second.m_strProcessedStr.size() <= iter->second.m_uLength);
            for (size_t i = 0; i < iter->second.m_uLength; ++i)
            {
                if (i < iter->second.m_strProcessedStr.size())
                {
                    char data = iter->second.m_strProcessedStr[i];
                    pData[uBaseAddress + i] = ~data;
                }
                else
                {
                    pData[uBaseAddress + i] = 0; //If the translated text length is less than the orginal text, fill the rest with 0.
                }
            }
        }
        startDataFile.Deserialize("../Resource/SourceFile/start_hack.DAT", "wb+");
        iconv_close(fd);
    }
	return 0;
}

