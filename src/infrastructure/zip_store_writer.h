/**
 * @file zip_store_writer.h
 * @brief P-28 报告模块地基：store（不压缩）模式 ZIP 写出器
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * DOCX/OPC 包本质是 ZIP。Qt 私有 QZipWriter 依赖 private 头文件路径，
 * 本写出器手写 store 模式（方法 0，不压缩）——OPC 规范对压缩无要求，
 * Word/WPS 完全兼容；零新增依赖、行为全确定、可单测 CRC/目录结构。
 */
#pragma once

#include <QString>
#include <QByteArray>
#include <QVector>

class ZipStoreWriter
{
public:
    /// 追加条目（名用正斜杠，如 "word/document.xml"；重复名后者覆盖前者）
    void addFile(const QString &name, const QByteArray &data);
    bool writeTo(const QString &path) const;

    static quint32 crc32(const QByteArray &data);   ///< CRC-32（查表法）

private:
    struct Entry {
        QString name;
        QByteArray data;
    };
    QVector<Entry> m_entries;
};
