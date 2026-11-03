// Renders an SVG into a multi-size Windows .ico (PNG-compressed entries).
//   elvideo-icongen <in.svg> <out.ico>
// Run after changing app/resources/elvideo.svg; the .ico is checked in.

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QtEndian>

#include <cstdio>
#include <vector>

namespace {

void appendLE16(QByteArray& out, quint16 v) {
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&v), 2);
}

void appendLE32(QByteArray& out, quint32 v) {
    v = qToLittleEndian(v);
    out.append(reinterpret_cast<const char*>(&v), 4);
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (argc != 3) {
        std::fprintf(stderr, "usage: elvideo-icongen <in.svg> <out.ico>\n");
        return 2;
    }

    QSvgRenderer renderer(QString::fromLocal8Bit(argv[1]));
    if (!renderer.isValid()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }

    // Sizes Windows picks from for title bars, taskbar, Explorer and Start.
    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<QByteArray> pngs;
    for (const int size : sizes) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
        painter.end();

        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        pngs.push_back(png);
    }

    // ICONDIR + ICONDIRENTRY[] + PNG payloads.
    QByteArray ico;
    appendLE16(ico, 0);  // reserved
    appendLE16(ico, 1);  // type: icon
    appendLE16(ico, static_cast<quint16>(pngs.size()));
    quint32 offset = 6 + 16 * static_cast<quint32>(pngs.size());
    for (std::size_t i = 0; i < pngs.size(); ++i) {
        const int size = sizes[i];
        ico.append(static_cast<char>(size >= 256 ? 0 : size));  // 0 means 256
        ico.append(static_cast<char>(size >= 256 ? 0 : size));
        ico.append('\0');  // palette
        ico.append('\0');  // reserved
        appendLE16(ico, 1);   // planes
        appendLE16(ico, 32);  // bits per pixel
        appendLE32(ico, static_cast<quint32>(pngs[i].size()));
        appendLE32(ico, offset);
        offset += static_cast<quint32>(pngs[i].size());
    }
    for (const auto& png : pngs) ico.append(png);

    QFile out(QString::fromLocal8Bit(argv[2]));
    if (!out.open(QIODevice::WriteOnly) || out.write(ico) != ico.size()) {
        std::fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    std::printf("wrote %s (%zu sizes, %lld bytes)\n", argv[2], pngs.size(), static_cast<long long>(ico.size()));
    return 0;
}
