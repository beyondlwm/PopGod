#include "stdafx.h"
class CControlCode;
std::map<uint32_t, CControlCode*> controlCodeMap;

class CControlCode
{
public:
    CControlCode(){};
    virtual ~CControlCode(){};

    virtual uint32_t GetParamSize() = 0;
    virtual uint32_t GetCode() = 0;
    virtual TString GetDesc() = 0;
};

class CNewLine : public CControlCode
{
public:
    CNewLine(){};
    virtual ~CNewLine(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF06CC00; };
    virtual TString GetDesc() override { return "[换行]"; };
};

class CDelay : public CControlCode
{
public:
    CDelay(){};
    virtual ~CDelay(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF06C900; };
    virtual TString GetDesc() override { return "[延迟]"; };
};

class CSetShowTextInterval : public CControlCode
{
public:
    CSetShowTextInterval(){};
    virtual ~CSetShowTextInterval(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF06CF00; };
    virtual TString GetDesc() override { return "[设置文字速度]"; };

};

class CShowPicture : public CControlCode
{
public:
    CShowPicture(){};
    virtual ~CShowPicture(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF08F501; };
    virtual TString GetDesc() override { return "[显示图片]"; };

};

class CSetTextColor : public CControlCode
{
public:
    CSetTextColor(){};
    virtual ~CSetTextColor(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0CD800; };
    virtual TString GetDesc() override { return "[设置文字颜色]"; };

};

class CSetPictureColor : public CControlCode
{
public:
    CSetPictureColor(){};
    virtual ~CSetPictureColor(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0CF701; };
    virtual TString GetDesc() override { return "[设置图片颜色]"; };
};

class CSetText : public CControlCode
{
public:
    CSetText(){};
    virtual ~CSetText(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0A0300; };
    virtual TString GetDesc() override { return "[设置文本]"; };

};

class CSetPicturePos : public CControlCode
{
public:
    CSetPicturePos(){};
    virtual ~CSetPicturePos(){};

    virtual uint32_t GetParamSize() override { return 12; };
    virtual uint32_t GetCode() override { return 0xFF0EF601; };
    virtual TString GetDesc() override { return "[设置图片位置]"; };
};

class CSetTextEnd : public CControlCode
{
public:
    CSetTextEnd(){};
    virtual ~CSetTextEnd(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF040400; };
    virtual TString GetDesc() override { return "[结束设置文本]"; };
};

class CUnknown1 : public CControlCode
{
public:
    CUnknown1(){};
    virtual ~CUnknown1(){};

    virtual uint32_t GetParamSize() override { return 12; };
    virtual uint32_t GetCode() override { return 0xFF10F901; };
    virtual TString GetDesc() override { return "[未知1]"; };

};

class CUnknown2 : public CControlCode
{
public:
    CUnknown2(){};
    virtual ~CUnknown2(){};

    virtual uint32_t GetParamSize() override { return 12; };
    virtual uint32_t GetCode() override { return 0xFF06D107; };
    virtual TString GetDesc() override { return "[未知2]"; };

};

class CUnknown3 : public CControlCode
{
public:
    CUnknown3(){};
    virtual ~CUnknown3(){};

    virtual uint32_t GetParamSize() override { return 20; };
    virtual uint32_t GetCode() override { return 0xFF16B104; };
    virtual TString GetDesc() override { return "[未知3]"; };

};

class CUnknown4 : public CControlCode
{
public:
    CUnknown4(){};
    virtual ~CUnknown4(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0A9203; };
    virtual TString GetDesc() override { return "[未知4]"; };

};

class CUnknown5 : public CControlCode
{
public:
    CUnknown5(){};
    virtual ~CUnknown5(){};

    virtual uint32_t GetParamSize() override { return 20; };
    virtual uint32_t GetCode() override { return 0xFF181505; };
    virtual TString GetDesc() override { return "[未知5]"; };

};

class CUnknown6 : public CControlCode
{
public:
    CUnknown6(){};
    virtual ~CUnknown6(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF04CB00; };
    virtual TString GetDesc() override { return "[未知6]"; };

};
class CUnknown7 : public CControlCode
{
public:
    CUnknown7(){};
    virtual ~CUnknown7(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF041605; };
    virtual TString GetDesc() override { return "[未知7]"; };

};

class CUnknown8 : public CControlCode
{
public:
    CUnknown8(){};
    virtual ~CUnknown8(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF08CA00; };
    virtual TString GetDesc() override { return "[未知8]"; };

}; 

class CUnknown9 : public CControlCode
{
public:
    CUnknown9(){};
    virtual ~CUnknown9(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF0CF801; };
    virtual TString GetDesc() override { return "[未知9]"; };

};

class CUnknown10 : public CControlCode
{
public:
    CUnknown10(){};
    virtual ~CUnknown10(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0C9301; };
    virtual TString GetDesc() override { return "[未知10]"; };

};

class CUnknown11 : public CControlCode
{
public:
    CUnknown11(){};
    virtual ~CUnknown11(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0C9501; };
    virtual TString GetDesc() override { return "[未知11]"; };

};

class CUnknown12 : public CControlCode
{
public:
    CUnknown12(){};
    virtual ~CUnknown12(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF0cd000; };
    virtual TString GetDesc() override { return "[未知12]"; };

};

class CUnknown13 : public CControlCode
{
public:
    CUnknown13(){};
    virtual ~CUnknown13(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF04d100; };
    virtual TString GetDesc() override { return "[未知13]"; };

};

class CUnknown14 : public CControlCode
{
public:
    CUnknown14(){};
    virtual ~CUnknown14(){};

    virtual uint32_t GetParamSize() override { return 0; };
    virtual uint32_t GetCode() override { return 0xFF0ed000; };
    virtual TString GetDesc() override { return "[未知14]"; };

};

class CUnknown15 : public CControlCode
{
public:
    CUnknown15(){};
    virtual ~CUnknown15(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF227905; };
    virtual TString GetDesc() override { return "[未知15]"; };

};

class CUnknown16 : public CControlCode
{
public:
    CUnknown16(){};
    virtual ~CUnknown16(){};

    virtual uint32_t GetParamSize() override { return 12; };
    virtual uint32_t GetCode() override { return 0xFF0E7B05; };
    virtual TString GetDesc() override { return "[未知16]"; };

}; 

class CUnknown17 : public CControlCode
{
public:
    CUnknown17(){};
    virtual ~CUnknown17(){};

    virtual uint32_t GetParamSize() override { return 8; };
    virtual uint32_t GetCode() override { return 0xFF0c7a05; };
    virtual TString GetDesc() override { return "[未知17]"; };

};


class CUnknown18 : public CControlCode
{
public:
    CUnknown18(){};
    virtual ~CUnknown18(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF082203; };
    virtual TString GetDesc() override { return "[未知18]"; };

};

class CUnknown19 : public CControlCode
{
public:
    CUnknown19(){};
    virtual ~CUnknown19(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF267905; };
    virtual TString GetDesc() override { return "[未知19]"; };

};

class CUnknown20 : public CControlCode
{
public:
    CUnknown20(){};
    virtual ~CUnknown20(){};

    virtual uint32_t GetParamSize() override { return 4; };
    virtual uint32_t GetCode() override { return 0xFF086a00; };
    virtual TString GetDesc() override { return "[未知20]"; };

};

void RegisterControlCode(CControlCode* pCode)
{
    BEATS_ASSERT(controlCodeMap.find(pCode->GetCode()) == controlCodeMap.end());
    controlCodeMap[pCode->GetCode()] = pCode;
}

void RegisterAllControlCode()
{
    RegisterControlCode(new CNewLine);
    RegisterControlCode(new CDelay);
    RegisterControlCode(new CSetShowTextInterval);
    RegisterControlCode(new CShowPicture);
    RegisterControlCode(new CSetTextColor);
    RegisterControlCode(new CSetPictureColor);
    RegisterControlCode(new CSetText);
    RegisterControlCode(new CSetPicturePos);
    RegisterControlCode(new CSetTextEnd);
    RegisterControlCode(new CUnknown1);
    RegisterControlCode(new CUnknown2);
    RegisterControlCode(new CUnknown3);
    RegisterControlCode(new CUnknown4);
    RegisterControlCode(new CUnknown5);
    RegisterControlCode(new CUnknown6);
    RegisterControlCode(new CUnknown7);
    RegisterControlCode(new CUnknown8);
    RegisterControlCode(new CUnknown9);
    RegisterControlCode(new CUnknown10);
    RegisterControlCode(new CUnknown11);
    RegisterControlCode(new CUnknown12);
    RegisterControlCode(new CUnknown13);
    RegisterControlCode(new CUnknown14);
    RegisterControlCode(new CUnknown15);
    RegisterControlCode(new CUnknown16);
    RegisterControlCode(new CUnknown17);
    RegisterControlCode(new CUnknown18);
    RegisterControlCode(new CUnknown19);
    RegisterControlCode(new CUnknown20);
}