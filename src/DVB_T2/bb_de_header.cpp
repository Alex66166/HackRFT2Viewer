/*
 *  Copyright 2020 Oleg Malyutin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "bb_de_header.h"

#include <QMessageBox>
#include <QTextStream>
#include <QTextCodec>

//#include <QDebug>

#define CRC_POLY 0xAB
#define CRC_POLYR 0xD5
#define TRANSPORT_ERROR_INDICATOR 0x80
#define BIT_PACKET_LENGTH (TRANSPORT_PACKET_LENGTH * 8)

//------------------------------------------------------------------------------------------
bb_de_header::bb_de_header(QObject *parent) :  QObject(parent)
{
    init_crc8_table();
    len = 53840 / 8 + TRANSPORT_PACKET_LENGTH * 2;//split tail ?
    out = new uint8_t[len];
    begin_out = out;
    buffer_out = new char[len];

    stream = new QDataStream;
    stream->setVersion(QDataStream::Qt_5_15);

    socket = new QUdpSocket(this);
    // Use an ephemeral loopback source port for the local player.
    if(!socket->bind(QHostAddress(QHostAddress::LocalHost), quint16(0)))
        qWarning("Unable to bind the local MPEG-TS UDP socket");
    metrics_timer.start();
}
//------------------------------------------------------------------------------------------
bb_de_header::~bb_de_header()
{
    if(socket->isOpen()) socket->close();
    delete socket;
    if(file != nullptr) {
        if(file->isOpen()) file->close();
        delete file;
    }
    delete stream;
    delete [] out;
    delete [] buffer_out;
}
//------------------------------------------------------------------------------------------
void bb_de_header::init_crc8_table()
{
    int r, crc;
    for (int i = 0; i < 256; ++i) {
        r = i;
        crc = 0;
        for (int j = 7; j >= 0; --j) {
            if ((r & (1 << j) ? 1 : 0) ^ ((crc & 0x80) ? 1 : 0)) crc = (crc << 1) ^ CRC_POLYR;
            else  crc <<= 1;
        }
        crc_table[i] = static_cast<uint8_t>(crc);
    }
}
//------------------------------------------------------------------------------------------
uint8_t  bb_de_header::check_crc8_mode(uint8_t *_in, int _len_in)
{
    uint8_t crc = 0;
    uint8_t b;
    int len_in = _len_in;
    uint8_t* in = _in;
    for (int i = 0; i < len_in; ++i) {
        b = in[i] ^ (crc & 0x01);
        crc >>= 1;
        if (b) crc ^= CRC_POLY;
    }
    return crc;
}
//------------------------------------------------------------------------------------------
void bb_de_header::execute(int index,l1_postsignalling post,int length,uint8_t *bits)
{
    if(!bits || length<80 || index<0 || index>=post.num_plp || !post.plp) return;
    ++metrics.bbFrames;
    if(!plps_published) {
        QStringList names; QList<int> indices;
        for(int i=0;i<post.num_plp;++i) { names.append(QStringLiteral("PLP ID %1").arg(post.plp[i].id)); indices.append(i); }
        emit plps_changed(names,indices); plps_published=true;
    }
    const int selected=need_plp>=0?need_plp:active_plp;
    if(selected>=0 && selected!=index)return;
    uint8_t remainder=check_crc8_mode(bits,80);
    if(remainder!=0 && remainder!=CRC_POLY) {
        ++metrics.bbHeaderErrors; bb_aligned=false; bb_packet.clear(); publish_metrics_if_due(); return;
    }
    const auto mode=remainder==0?INPUTMODE_NORMAL:INPUTMODE_HIEFF;
    int pos=0;
    auto read=[&](int count) { int value=0; while(count--)value=(value<<1)|(bits[pos++]&1); return value; };
    bb_header h{};
    h.ts_gs=read(2);h.sis_mis=read(1);h.ccm_acm=read(1);h.issyi=read(1);h.npd=read(1);h.ext=read(2);h.isi=read(8);
    h.upl=read(16);h.dfl=read(16);h.sync=read(8);h.syncd=read(16);read(8);
    if(active_plp<0 && need_plp<0 && h.ts_gs==TS_GS_TRANSPORT && (post.num_plp==1 || post.plp[index].plp_type!=0)) active_plp=index;
    if((need_plp>=0?need_plp:active_plp)!=index) return;
    if(!info_already_set) set_info(index,post,mode,h);
    if(h.ts_gs!=TS_GS_TRANSPORT || h.npd || (mode==INPUTMODE_NORMAL && (h.issyi || h.upl!=1504)) ||
       h.dfl>length-80 || (h.dfl&7) || (h.syncd!=65535 && ((h.syncd&7) || h.syncd>h.dfl))) {
        ++metrics.bbHeaderErrors; bb_aligned=false; bb_packet.clear(); publish_metrics_if_due(); return;
    }
    const bool normal=mode==INPUTMODE_NORMAL;
    const int bytes=h.dfl/8,offset=h.syncd/8;
    if(bb_mode!=int(mode)){bb_mode=int(mode);bb_aligned=false;bb_packet.clear();}
    if(bb_aligned && h.syncd!=65535) {
        const int expected=bb_packet.isEmpty()?0:187-bb_packet.size();
        if(expected!=offset){bb_aligned=false;bb_packet.clear();++metrics.syncLosses;}
    }
    int begin=0;
    if(!bb_aligned) {
        if(h.syncd==65535)return;
        begin=offset+(normal?1:0);
        if(begin>bytes)return;
        bb_aligned=true;bb_crc=0;
    }
    auto sendPacket=[&](bool damaged) {
        QByteArray packet(1,char(0x47));packet.append(bb_packet);
        if(damaged)packet[1]=char(uint8_t(packet[1])|TRANSPORT_ERROR_INDICATOR);
        queue_transport_bytes(packet.constData(),packet.size());
        bb_packet.clear();bb_crc=0;
    };
    for(int i=begin;i<bytes;++i) {
        uint8_t value=0;for(int b=0;b<8;++b)value=uint8_t((value<<1)|bits[80+i*8+b]);
        if(normal && bb_packet.size()==187){sendPacket(value!=bb_crc);continue;}
        bb_packet.append(char(value));bb_crc=crc_table[value^bb_crc];
        if(!normal && bb_packet.size()==187)sendPacket(false);
    }
    publish_metrics_if_due();
}
// Send seven complete 188-byte MPEG-TS packets per UDP datagram.
void bb_de_header::queue_transport_bytes(const char *data, int length)
{
    if(data == nullptr || length <= 0) return;
    pending_ts.append(data, length);

    while(pending_ts.size() >= TRANSPORT_PACKET_LENGTH) {
        int sync = pending_ts.indexOf(char(0x47));
        if(sync < 0) {
            metrics.syncLosses += static_cast<quint64>(pending_ts.size());
            pending_ts.clear();
            break;
        }
        if(sync > 0) {
            metrics.syncLosses += static_cast<quint64>(sync);
            pending_ts.remove(0, sync);
            if(pending_ts.size() < TRANSPORT_PACKET_LENGTH) break;
        }

        // Confirm the next sync byte when it is already available.  A stray
        // 0x47 in damaged payload must not become a false packet boundary.
        if(pending_ts.size() >= TRANSPORT_PACKET_LENGTH * 2 &&
           static_cast<uint8_t>(pending_ts.at(TRANSPORT_PACKET_LENGTH)) != 0x47) {
            pending_ts.remove(0, 1);
            ++metrics.syncLosses;
            continue;
        }

        QByteArray packet = pending_ts.left(TRANSPORT_PACKET_LENGTH);
        pending_ts.remove(0, TRANSPORT_PACKET_LENGTH);
        process_transport_packet(reinterpret_cast<const uint8_t*>(packet.constData()));

        if(recording_enabled && stream != nullptr && stream->device() != nullptr)
            stream->writeRawData(packet.constData(), packet.size());

        if(network_enabled) {
            udp_datagram.append(packet);
            if(udp_datagram.size() == TRANSPORT_PACKET_LENGTH * 7) {
                socket->writeDatagram(udp_datagram, QHostAddress::LocalHost, num_port_udp);
                udp_datagram.clear();
            }
        }
    }
    publish_metrics_if_due();
}

void bb_de_header::process_transport_packet(const uint8_t *packet)
{
    if(packet == nullptr || packet[0] != 0x47) {
        ++metrics.syncLosses;
        return;
    }

    ++metrics.packets;
    metrics_bytes += TRANSPORT_PACKET_LENGTH;
    if(packet[1] & 0x80) ++metrics.transportErrors;
    if((packet[3] & 0xC0) != 0) ++metrics.scrambledPackets;

    const int pid = ((packet[1] & 0x1F) << 8) | packet[2];
    const int adaptationControl = (packet[3] >> 4) & 0x03;
    const bool hasPayload = adaptationControl == 1 || adaptationControl == 3;
    const int continuity = packet[3] & 0x0F;
    if(hasPayload && pid != 0x1FFF) {
        if(continuity_by_pid.contains(pid)) {
            const int expected = (continuity_by_pid.value(pid) + 1) & 0x0F;
            if(continuity != expected) ++metrics.continuityErrors;
        }
        continuity_by_pid.insert(pid, continuity);
    }

    if(!hasPayload || (pid != 0x0000 && pid != 0x0011)) return;
    int offset = 4;
    if(adaptationControl == 3) {
        offset += 1 + packet[4];
        if(offset >= TRANSPORT_PACKET_LENGTH) return;
    }
    process_psi_payload(pid, packet + offset, TRANSPORT_PACKET_LENGTH - offset,
                        (packet[1] & 0x40) != 0);
}

void bb_de_header::process_psi_payload(int pid, const uint8_t *data, int length,
                                       bool payloadStart)
{
    if(data == nullptr || length <= 0) return;
    QByteArray &buffer = psi_buffers[pid];
    int offset = 0;

    if(payloadStart) {
        const int pointer = data[0];
        offset = 1;
        if(pointer > length - offset) {
            buffer.clear();
            return;
        }

        // Bytes before the first new section complete a section begun in the
        // previous TS packet.  A zero pointer starts a fresh section now.
        if(!buffer.isEmpty() && pointer > 0)
            buffer.append(reinterpret_cast<const char*>(data + offset), pointer);

        while(buffer.size() >= 3) {
            const uint8_t *section = reinterpret_cast<const uint8_t*>(buffer.constData());
            const int totalLength = 3 + (((section[1] & 0x0F) << 8) | section[2]);
            if(totalLength < 8 || totalLength > 4096 || buffer.size() < totalLength) break;
            process_psi_section(pid, section, totalLength);
            buffer.remove(0, totalLength);
        }

        // Whatever remains belongs to a broken old section and must not be
        // joined to the new section following the pointer.
        buffer.clear();
        offset += pointer;
    }

    if(offset < length)
        buffer.append(reinterpret_cast<const char*>(data + offset), length - offset);

    while(buffer.size() >= 3) {
        if(static_cast<uint8_t>(buffer.at(0)) == 0xFF) {
            buffer.clear();
            break;
        }
        const uint8_t *section = reinterpret_cast<const uint8_t*>(buffer.constData());
        const int totalLength = 3 + (((section[1] & 0x0F) << 8) | section[2]);
        if(totalLength < 8 || totalLength > 4096) {
            buffer.remove(0, 1);
            continue;
        }
        if(buffer.size() < totalLength) break;
        process_psi_section(pid, section, totalLength);
        buffer.remove(0, totalLength);
    }
}

void bb_de_header::process_psi_section(int pid, const uint8_t *data, int length)
{
    if(length < 3) return;
    const int sectionLength = ((data[1] & 0x0F) << 8) | data[2];
    const int totalLength = 3 + sectionLength;
    if(totalLength > length || totalLength < 8) return;
    if(!(data[1]&0x80) || !(data[5]&1))return;
    quint32 sectionCrc=0xffffffffU;
    for(int i=0;i<totalLength;++i) {
        sectionCrc^=quint32(data[i])<<24;
        for(int b=0;b<8;++b)sectionCrc=(sectionCrc<<1)^((sectionCrc&0x80000000U)?0x04c11db7U:0U);
    }
    if(sectionCrc!=0)return;
    if(pid == 0x0000 && data[0] == 0x00)
        parse_pat(data, totalLength);
    else if(pid == 0x0011 && (data[0] == 0x42 || data[0] == 0x46))
        parse_sdt(data, totalLength);
}

void bb_de_header::parse_pat(const uint8_t *section, int length)
{
    bool changed = false;
    for(int p = 8; p + 4 <= length - 4; p += 4) {
        const int serviceId = (section[p] << 8) | section[p + 1];
        if(serviceId == 0) continue;
        if(!services.contains(serviceId)) {
            services.insert(serviceId, QStringLiteral("Сервис %1").arg(serviceId));
            changed = true;
        }
    }
    if(changed) publish_services();
}

void bb_de_header::parse_sdt(const uint8_t *section, int length)
{
    bool changed = false;
    int p = 11;
    const int end = length - 4;
    while(p + 5 <= end) {
        const int serviceId = (section[p] << 8) | section[p + 1];
        const int descriptorsLength = ((section[p + 3] & 0x0F) << 8) | section[p + 4];
        int d = p + 5;
        const int descriptorsEnd = qMin(d + descriptorsLength, end);
        QString serviceName;
        while(d + 2 <= descriptorsEnd) {
            const int tag = section[d];
            const int descriptorLength = section[d + 1];
            if(d + 2 + descriptorLength > descriptorsEnd) break;
            if(tag == 0x48 && descriptorLength >= 3) {
                const uint8_t *descriptor = section + d + 2;
                const int providerLength = descriptor[1];
                const int nameOffset = 2 + providerLength;
                if(nameOffset < descriptorLength) {
                    const int nameLength = descriptor[nameOffset];
                    if(nameOffset + 1 + nameLength <= descriptorLength)
                        serviceName = decode_dvb_text(descriptor + nameOffset + 1, nameLength);
                }
            }
            d += 2 + descriptorLength;
        }
        if(serviceName.isEmpty())
            serviceName = QStringLiteral("Сервис %1").arg(serviceId);
        if(services.value(serviceId) != serviceName) {
            services.insert(serviceId, serviceName);
            changed = true;
        }
        p += 5 + descriptorsLength;
    }
    if(changed) publish_services();
}

QString bb_de_header::decode_dvb_text(const uint8_t *data, int length) const
{
    if(data == nullptr || length <= 0) return QString();
    if(data[0] == 0x15)
        return QString::fromUtf8(reinterpret_cast<const char*>(data + 1), length - 1);
    if(data[0] == 0x01) {
        QTextCodec *codec = QTextCodec::codecForName("ISO-8859-5");
        if(codec != nullptr)
            return codec->toUnicode(reinterpret_cast<const char*>(data + 1), length - 1);
    }
    return QString::fromLatin1(reinterpret_cast<const char*>(data), length).trimmed();
}

void bb_de_header::publish_services()
{
    QStringList names;
    QList<int> ids;
    for(auto it = services.cbegin(); it != services.cend(); ++it) {
        ids.append(it.key());
        names.append(it.value());
    }
    emit services_changed(names, ids);
}

void bb_de_header::publish_metrics_if_due()
{
    const qint64 elapsed = metrics_timer.elapsed();
    if(elapsed < 1000) return;
    metrics.bitrateMbps = static_cast<double>(metrics_bytes) * 8.0 /
                          (static_cast<double>(elapsed) * 1000.0);
    emit transport_metrics(metrics);
    metrics_bytes = 0;
    metrics_timer.restart();
}
//_____________________________________________________________________________________________
void bb_de_header::set_info(int _plp_id, l1_postsignalling _l1_post,
                            dvbt2_inputmode_t mode, bb_header header)
{
    if(_plp_id != next_plp_info) return;

    QString temp;
    info += "PLP :\t" + QString::number(_plp_id) + "\n";
    if(mode == INPUTMODE_HIEFF) temp = "HEM";
    else temp = "NM";
    info += "Mode\t\t" + temp + "\n";
    switch(header.ts_gs){
    case 0:
        temp = "GFPS(not supported)";
    break;
    case 1:
        temp = "GCS(not supported)";
    break;
    case 2:
        temp = "GSE(not supported)";
    break;
    case 3:
        temp = "TS";
    break;
    default:
        temp = "unknow";
    break;
    }
    info += "TS/GS\t\t" + temp + "\n";
    if(header.sis_mis) temp = "single";
    else temp = "multiple";
    info += "SIS/MIS\t\t " + temp + "\n";
    if(header.issyi) temp = "yes";
    else temp = "no";
    info += "ISSYI\t\t" + temp + "\n";
    if(header.npd)temp = "yes";
    else temp = "no";
    info += "NDP\t\t" + temp;

    ++next_plp_info;
    if(next_plp_info == _l1_post.num_plp) {
        next_plp_info = 0;
        info_already_set = true;
        emit ts_stage(info);
        info = "";
    }
    else{
        info += "\n";
    }
}
//_____________________________________________________________________________________________
void bb_de_header::set_out(id_out _id_current_out, int _num_port_udp,
                           QString _file_name, int _need_plp)
{
    id_current_out = _id_current_out;
    file_name = _file_name;
    num_port_udp = static_cast<unsigned short>(_num_port_udp);
    need_plp = _need_plp;
    if(id_current_out == out_file){
        if(file != nullptr) {
            if(file->isOpen()) file->close();
            delete file;
        }
        file = new QFile(file_name);
        if(file->open(QIODevice::WriteOnly)) {
            stream->setDevice(file);
        }
        else{
            QMessageBox::information(nullptr, "error", file->errorString());
        }
    }
    else{
        if(file != nullptr) {
            if(file->isOpen()) file->close();
        }
    }
}

void bb_de_header::set_network_output(bool enabled, int port)
{
    network_enabled = enabled;
    if(port > 0 && port <= 65535)
        num_port_udp = static_cast<unsigned short>(port);
    if(!enabled) udp_datagram.clear();
}

void bb_de_header::set_recording(bool enabled, QString fileName)
{
    recording_enabled = false;
    if(file != nullptr) {
        if(file->isOpen()) file->close();
        delete file;
        file = nullptr;
    }
    stream->setDevice(nullptr);
    if(!enabled) return;

    file = new QFile(fileName);
    if(file->open(QIODevice::WriteOnly)) {
        stream->setDevice(file);
        recording_enabled = true;
    }
    else {
        emit ts_stage(QStringLiteral("Не удалось открыть файл записи: %1")
                      .arg(file->errorString()));
    }
}

void bb_de_header::reset_transport_state()
{
    active_plp=-1; bb_aligned=false; bb_mode=-1; bb_crc=0; bb_packet.clear();
    pending_ts.clear();
    udp_datagram.clear();
    continuity_by_pid.clear();
    psi_buffers.clear();
    services.clear();
    metrics = TransportMetrics{};
    metrics_bytes = 0;
    metrics_timer.restart();
    emit services_changed(QStringList(), QList<int>());
    emit transport_metrics(metrics);
}
//_____________________________________________________________________________________________
void bb_de_header::stop()
{
    emit finished();
}
//_____________________________________________________________________________________________
