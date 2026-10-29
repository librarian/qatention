#pragma once
#include <QString>
namespace aten {
inline quint32 characterUsage(QChar c,bool &shift) {
    shift=false;
    if(c>='a' && c<='z') return 4+c.unicode()-'a';
    if(c>='A' && c<='Z') {shift=true;return 4+c.unicode()-'A';}
    if(c>='1' && c<='9') return 0x1e + c.unicode()-'1';
    if(c=='0') return 0x27;if(c==' ') return 0x2c;if(c=='\t') return 0x2b;if(c=='\n') return 0x28;
    const QString normal=QStringLiteral("1234567890-=[]\\;'")+QChar(96)+",./",upper="!@#$%^&*()_+{}|:\"~<>?";
    const quint32 usages[]={0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x2d,0x2e,0x2f,0x30,0x31,0x33,0x34,0x35,0x36,0x37,0x38};
    int i=normal.indexOf(c);if(i>=0) return usages[i];i=upper.indexOf(c);if(i>=0) {shift=true;return usages[i];}return 0;
}
}
