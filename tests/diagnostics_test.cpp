#include "diagnostics.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <cassert>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);QTemporaryDir dir;assert(dir.isValid());
    const QString path=dir.filePath("bounded.log");
    for(int i=0;i<20;++i)Diagnostics::appendBounded(path,QByteArray(40,'x'),100);
    assert(QFileInfo(path).size()<=100);assert(QFileInfo(path+".1").size()<=100);
    assert(QDir(dir.path()).entryList(QDir::Files).size()==2);
    Diagnostics::appendBounded(path,QByteArray(1000,'y'),100);
    assert(QFileInfo(path).size()==100);
    puts("Bounded diagnostic log rotation and oversized message PASS");
}
