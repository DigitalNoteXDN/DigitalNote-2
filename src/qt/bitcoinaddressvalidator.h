#ifndef ADDRESSVALIDATOR_H
#define ADDRESSVALIDATOR_H

#include <QValidator>

/** Base48 entry widget validator.
   Corrects near-miss characters and refuses characters that are no part of base48.
 */
class DigitalNoteAddressValidator : public QValidator
{
    Q_OBJECT

public:
    explicit DigitalNoteAddressValidator(QObject *parent = 0);

    State validate(QString &input, int &pos) const;

    static const int MaxAddressLength = 128;
};

#endif // ADDRESSVALIDATOR_H
