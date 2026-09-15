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
    QFile csv(dir.filePath("stages.csv"));assert(csv.open(QIODevice::WriteOnly));
    csv.write("elapsed_ms,resampler_ms\n");
    for(int i=0;i<1000;++i)csv.write(QStringLiteral("%1,12.5\n").arg(i).toUtf8());
    csv.close();
    const auto tail=Diagnostics::textTail(csv.fileName(),true,128);
    assert(tail.startsWith("elapsed_ms,resampler_ms\n"));assert(tail.endsWith("999,12.5\n"));
    for(const auto &row:tail.split('\n',Qt::SkipEmptyParts))assert(row.count(',')==1);
    assert(tail.toUtf8().size()<=128+24);
    assert(!Diagnostics::cpuDescription().isEmpty());
    puts("Bounded diagnostic log rotation and oversized message PASS");
    puts("Self-contained CSV tail, complete rows and CPU identification PASS");
}
