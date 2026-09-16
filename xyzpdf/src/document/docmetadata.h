#pragma once

#include <QDateTime>
#include <QString>

// What the Document Properties dialog shows. Filled in from whatever the file
// actually carries: a PDF is free to have none of these, and the dialog says so
// rather than showing an empty shell.
struct DocMetadata
{
    QString title;
    QString author;
    QString subject;
    QString keywords;
    // The program that made the PDF and the program that made the original
    // document, which are routinely different and routinely interesting when
    // something does not render.
    QString producer;
    QString creator;
    // Encrypted documents report these; unencrypted ones leave both false.
    // There is no way to ask a PDF "what may I do" that does not involve the
    // owner password, so this is only what the file volunteers.
    bool encrypted = false;
    QDateTime creationDate;
    QDateTime modificationDate;

    bool isEmpty() const
    {
        return title.isEmpty() && author.isEmpty() && subject.isEmpty() && keywords.isEmpty()
               && producer.isEmpty() && creator.isEmpty() && !encrypted
               && !creationDate.isValid() && !modificationDate.isValid();
    }
};
