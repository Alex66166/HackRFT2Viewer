#include <QCoreApplication>
#include <QDebug>
#include <QUdpSocket>
#include <QTemporaryDir>
#include <cassert>
#include <vector>
#include "DVB_T2/bb_de_header.h"
class TransportOutputTest {
public:
 static void feed(bb_de_header &h,const QByteArray &bytes){h.queue_transport_bytes(bytes.constData(),bytes.size());}
 static quint64 badHeaders(const bb_de_header &h){return h.metrics.bbHeaderErrors;}
};
static uint8_t crc8(const QByteArray &data){
 uint8_t crc=0;for(unsigned char b:data){crc^=b;for(int j=0;j<8;++j)crc=uint8_t((crc<<1)^((crc&0x80)?0xD5:0));}return crc;
}
static uint32_t crc32(const QByteArray &data){
 uint32_t crc=0xffffffff;for(unsigned char b:data){crc^=uint32_t(b)<<24;for(int j=0;j<8;++j)crc=(crc<<1)^((crc&0x80000000)?0x04c11db7:0);}return crc;
}
static QByteArray nullPacket(int counter){
 QByteArray packet(188,char(0xff));packet[0]=0x47;packet[1]=0x1f;packet[2]=char(0xff);packet[3]=char(0x10|(counter&15));return packet;
}
static QByteArray patPacket(){
 QByteArray packet(188,char(0xff));packet[0]=0x47;packet[1]=0x40;packet[2]=0;packet[3]=0x10;packet[4]=0;
 QByteArray section=QByteArray::fromHex("00b00d0001c100000001e100");uint32_t crc=crc32(section);
 for(int i=24;i>=0;i-=8)section.append(char(crc>>i));packet.replace(5,section.size(),section);return packet;
}
static std::vector<uint8_t> bbFrame(const QByteArray &payload,int syncd,bool normal){
 QByteArray head;head.append(char(0xf0));head.append(char(0));int upl=normal?1504:0;
 head.append(char(upl>>8));head.append(char(upl));head.append(char(payload.size()*8>>8));head.append(char(payload.size()*8));
 head.append(char(0x47));head.append(char(syncd>>8));head.append(char(syncd));head.append(char(crc8(head)^(normal?0:1)));
 head+=payload;std::vector<uint8_t> bits(head.size()*8);for(int i=0;i<int(bits.size());++i)bits[i]=(uint8_t(head[i/8])>>(7-i%8))&1;return bits;
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);QTemporaryDir dir;assert(dir.isValid());
 QUdpSocket receive;bool udp=receive.bind(QHostAddress(QHostAddress::LocalHost),quint16(0));
 if(!udp)qWarning()<<"UDP test unavailable:"<<receive.errorString();
 QByteArray stream=patPacket();for(int i=1;i<7;++i)stream+=nullPacket(i);
 for(bool normal:{true,false}){
  bb_de_header output;output.set_network_output(udp,udp?receive.localPort():17654);
  QString path=dir.filePath(normal?"normal.ts":"he.ts");output.set_recording(true,path);bool service=false;
  QObject::connect(&output,&bb_de_header::services_changed,&app,[&](QStringList,QList<int> ids){service=ids.contains(1);});
  l1_postsignalling_plp plp;plp.id=17;plp.plp_type=1;l1_postsignalling post;post.num_plp=1;post.plp=&plp;
  QByteArray adapted;if(normal)adapted.append(char(0));
  for(int i=0;i<7;++i){QByteArray packet=stream.mid(i*188+1,187);adapted+=packet;if(normal)adapted.append(char(crc8(packet)));}
  int period=normal?188:187;
  for(int pos=0;pos<adapted.size();pos+=211){
   QByteArray part=adapted.mid(pos,211);int offset=(period-pos%period)%period;
   auto bits=bbFrame(part,offset>part.size()?65535:offset*8,normal);output.execute(0,post,bits.size(),bits.data());
  }
  output.set_recording(false,QString());QFile file(path);assert(file.open(QIODevice::ReadOnly));
  assert(file.readAll()==stream);assert(service);assert(TransportOutputTest::badHeaders(output)==0);
  if(udp){
   assert(receive.hasPendingDatagrams()||receive.waitForReadyRead(1000));QByteArray datagram(receive.pendingDatagramSize(),0);
   assert(receive.readDatagram(datagram.data(),datagram.size())==1316);assert(datagram==stream);
  }
  qInfo()<<"BBFRAME split packets, PLP ID17, normal"<<normal<<"file/PSI PASS; UDP"<<(udp?"PASS":"UNAVAILABLE");
 }
 qInfo()<<"transport_output_test PASS";
}
