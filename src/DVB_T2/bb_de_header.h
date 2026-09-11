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
#ifndef BB_DE_HEADER_H
#define BB_DE_HEADER_H

#include <QObject>
#include <QUdpSocket>
#include <QFile>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QMap>
#include <QStringList>

#include "dvbt2_definition.h"

#define BB_HEADER_LENGTH_BITS 80
#define TS_GS_TRANSPORT          3
#define TS_GS_GENERIC_PACKETIZED 0
#define TS_GS_GENERIC_CONTINUOUS 1
#define TS_GS_RESERVED           2
#define SIS_MIS_SINGLE   1
#define SIS_MIS_MULTIPLE 0
#define CCM 1
#define ACM 0
#define ISSYI_ACTIVE     1
#define ISSYI_NOT_ACTIVE 0
#define NPD_ACTIVE       1
#define NPD_NOT_ACTIVE   0

#define TRANSPORT_PACKET_LENGTH 188

struct TransportMetrics
{
    double bitrateMbps = 0.0;
    quint64 packets = 0;
    quint64 bbFrames=0,bbHeaderErrors=0,bchFrames=0,bchCorrectedBits=0,bchFailedFrames=0;
    quint64 transportErrors = 0;
    quint64 continuityErrors = 0;
    quint64 syncLosses = 0;
    quint64 scrambledPackets = 0;
};

Q_DECLARE_METATYPE(TransportMetrics)

class bb_de_header : public QObject
{
    Q_OBJECT
    friend class TransportOutputTest;
public:
    explicit bb_de_header(QObject *parent = nullptr);
    ~bb_de_header();

    enum id_out{
        out_network = 0,
        out_file,
    };

signals:
    void plps_changed(QStringList names,QList<int> indices);
    void finished();
    void ts_stage(QString _info);
    void transport_metrics(TransportMetrics metrics);
    void services_changed(QStringList names, QList<int> serviceIds);

public slots:
    void select_plp(int index) { if(need_plp!=index) { need_plp=index; reset_transport_state(); } }
    void set_bch_metrics(quint64 frames,quint64 corrected,quint64 failed) {
        metrics.bchFrames=frames; metrics.bchCorrectedBits=corrected; metrics.bchFailedFrames=failed; publish_metrics_if_due();
    }
    void execute(int _plp_id, l1_postsignalling _l1_post, int _len_in, uint8_t* _in);
    void set_out(bb_de_header::id_out _id_current_out, int _num_port_udp, QString _file_name, int _need_plp);
    void set_network_output(bool enabled, int port);
    void set_recording(bool enabled, QString fileName);
    void reset_transport_state();
    void stop();
    TransportMetrics snapshotMetrics() const { return metrics; }

private:
    int plp_id = 0;
    uint8_t  crc = 0;
    uint8_t crc_table[256];
    void init_crc8_table();
    uint8_t check_crc8_mode(uint8_t *_in, int _len_in);
    struct bb_header{
        int ts_gs;
        int sis_mis;
        int ccm_acm;
        int issyi;
        int npd;
        int ext;
        int isi;
        int upl;
        int dfl;
        int sync;
        int syncd;
    };
    int idx_packet = 0;
    int idx_buffer = 0;
    bool split = false;
    uint8_t buffer[TRANSPORT_PACKET_LENGTH];

    int need_plp = -1;
    int active_plp=-1,bb_mode=-1;
    bool plps_published=false,bb_aligned=false;
    QByteArray bb_packet;
    uint8_t bb_crc=0;
    uint8_t* begin_out;
    uint8_t* out;
    int len;
    char* buffer_out;
    QFile* file = nullptr;
    QDataStream* stream;
    QUdpSocket* socket;
    int id_current_out = out_network;
    QString file_name = "out_dvbt2.ts";
    unsigned short num_port_udp = 7654;

    bool network_enabled = true;
    bool recording_enabled = false;
    QByteArray pending_ts;
    QByteArray udp_datagram;
    QElapsedTimer metrics_timer;
    quint64 metrics_bytes = 0;
    TransportMetrics metrics;
    QHash<int, int> continuity_by_pid;
    QHash<int, QByteArray> psi_buffers;
    QMap<int, QString> services;

    void queue_transport_bytes(const char *data, int length);
    void process_transport_packet(const uint8_t *packet);
    void process_psi_payload(int pid, const uint8_t *data, int length, bool payloadStart);
    void process_psi_section(int pid, const uint8_t *data, int length);
    void parse_pat(const uint8_t *section, int length);
    void parse_sdt(const uint8_t *section, int length);
    QString decode_dvb_text(const uint8_t *data, int length) const;
    void publish_metrics_if_due();
    void publish_services();

    bool info_already_set = false;
    QString info = "";
    int next_plp_info = 0;
    void set_info(int _plp_id, l1_postsignalling _l1_post, dvbt2_inputmode_t mode, bb_header header);
};

#endif // BB_DE_HEADER_H
