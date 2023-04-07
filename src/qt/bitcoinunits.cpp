#include "compat.h"

#include <QSettings>
#include <QStringList>

#include "main_const.h"

#include "bitcoinunits.h"

DigitalNoteUnits::DigitalNoteUnits(QObject *parent)
		: QAbstractListModel(parent), unitlist(availableUnits())
{
	
}

QList<DigitalNoteUnits::Unit> DigitalNoteUnits::availableUnits()
{
	QList<DigitalNoteUnits::Unit> unitlist;

	unitlist.append(XDN);
	unitlist.append(mXDN);
	unitlist.append(uXDN);

	return unitlist;
}

bool DigitalNoteUnits::valid(int unit)
{
	switch(unit)
	{
		case XDN:
		case mXDN:
		case uXDN:
			return true;
		default:
			return false;
	}
}

QString DigitalNoteUnits::name(int unit)
{
	switch(unit)
	{
		case XDN: return QString("XDN");
		case mXDN: return QString("mXDN");
		case uXDN: return QString::fromUtf8("μXDN");
		default: return QString("???");
	}
}

QString DigitalNoteUnits::description(int unit)
{
    switch(unit)
    {
		case XDN: return QString("DigitalNotes");
		case mXDN: return QString("Milli-DigitalNotes (1 / 1,000)");
		case uXDN: return QString("Micro-DigitalNotes (1 / 1,000,000)");
		default: return QString("???");
    }
}

qint64 DigitalNoteUnits::factor(int unit)
{
    switch(unit)
    {
		case XDN:  return 100000000;
		case mXDN: return 100000;
		case uXDN: return 100;
		default:   return 100000000;
    }
}

int DigitalNoteUnits::amountDigits(int unit)
{
    switch(unit)
    {
		case XDN: return 11;  // 21,000,000,000         (# digits, without commas)
		case mXDN: return 14; // 21,000,000,000,000
		case uXDN: return 17; // 21,000,000,000,000,000
		default: return 0;
    }
}

int DigitalNoteUnits::decimals(int unit)
{
    switch(unit)
    {
		case XDN: return 8;
		case mXDN: return 5;
		case uXDN: return 2;
		default: return 0;
    }
}

QString DigitalNoteUnits::format(int unit, const CAmount& nIn, bool fPlus, SeparatorStyle separators)
{
	// Note: not using straight sprintf here because we do NOT want
	// localized number formatting.
	if(!valid(unit))
	{
		return QString(); // Refuse to format invalid unit
	}
	
	qint64 n = (qint64)nIn;
	qint64 coin = factor(unit);
	int num_decimals = decimals(unit);
	qint64 n_abs = (n > 0 ? n : -n);
	qint64 quotient = n_abs / coin;
	qint64 remainder = n_abs % coin;
	QString quotient_str = QString::number(quotient);
	QString remainder_str = QString::number(remainder).rightJustified(num_decimals, '0');

	// Right-trim excess zeros after the decimal point
	int nTrim = 0;
	for (int i = remainder_str.size()-1; i>=2 && (remainder_str.at(i) == '0'); --i)
	{
		++nTrim;
	}
	
	remainder_str.chop(nTrim);

	if (n < 0)
	{
		quotient_str.insert(0, '-');
	}
	else if (fPlus && n > 0)
	{
		quotient_str.insert(0, '+');
	}
	
	return quotient_str + QString(".") + remainder_str;
}

// TODO: Review all remaining calls to DigitalNoteUnits::formatWithUnit to
// TODO: determine whether the output is used in a plain text context
// TODO: or an HTML context (and replace with
// TODO: BtcoinUnits::formatHtmlWithUnit in the latter case). Hopefully
// TODO: there aren't instances where the result could be used in
// TODO: either context.

// NOTE: Using formatWithUnit in an HTML context risks wrapping
// quantities at the thousands separator. More subtly, it also results
// in a standard space rather than a thin space, due to a bug in Qt's
// XML whitespace canonicalisation
//
// Please take care to use formatHtmlWithUnit instead, when
// appropriate.

QString DigitalNoteUnits::formatWithUnit(int unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
	return format(unit, amount, plussign, separators) + QString(" ") + name(unit);
}

QString DigitalNoteUnits::formatHtmlWithUnit(int unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
	QString str(formatWithUnit(unit, amount, plussign, separators));

	str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));

	return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

QString DigitalNoteUnits::floorWithUnit(int unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
	QSettings settings;
	int digits = settings.value("digits").toInt();

	QString result = format(unit, amount, plussign, separators);

	if(decimals(unit) > digits)
	{
		result.chop(decimals(unit) - digits);
	}

	return result + QString(" ") + name(unit);
}

QString DigitalNoteUnits::floorHtmlWithUnit(int unit, const CAmount& amount, bool plussign, SeparatorStyle separators)
{
	QString str(floorWithUnit(unit, amount, plussign, separators));

	str.replace(QChar(THIN_SP_CP), QString(THIN_SP_HTML));

	return QString("<span style='white-space: nowrap;'>%1</span>").arg(str);
}

bool DigitalNoteUnits::parse(int unit, const QString &value, CAmount *val_out)
{
	if(!valid(unit) || value.isEmpty())
	{
		return false; // Refuse to parse invalid unit or empty string
	}
	
	int num_decimals = decimals(unit);

	// Ignore spaces and thin spaces when parsing
	QStringList parts = removeSpaces(value).split(".");

	if(parts.size() > 2)
	{
		return false; // More than one dot
	}
	
	QString whole = parts[0];
	QString decimals;

	if(parts.size() > 1)
	{
		decimals = parts[1];
	}
	
	if(decimals.size() > num_decimals)
	{
		return false; // Exceeds max precision
	}
	
	bool ok = false;
	QString str = whole + decimals.leftJustified(num_decimals, '0');

	if(str.size() > 18)
	{
		return false; // Longer numbers will exceed 63 bits
	}
	
	CAmount retvalue = str.toLongLong(&ok);
	
	if(val_out)
	{
		*val_out = retvalue;
	}
	
	return ok;
}

QString DigitalNoteUnits::getAmountColumnTitle(int unit)
{
	QString amountTitle = QObject::tr("Amount");

	if (DigitalNoteUnits::valid(unit))
	{
		amountTitle += " ("+DigitalNoteUnits::name(unit) + ")";
	}

	return amountTitle;
}

int DigitalNoteUnits::rowCount(const QModelIndex &parent) const
{
	Q_UNUSED(parent);

	return unitlist.size();
}

QVariant DigitalNoteUnits::data(const QModelIndex &index, int role) const
{
	int row = index.row();

	if(row >= 0 && row < unitlist.size())
	{
		Unit unit = unitlist.at(row);
		switch(role)
		{
			case Qt::EditRole:
			case Qt::DisplayRole:
				return QVariant(name(unit));
			case Qt::ToolTipRole:
				return QVariant(description(unit));
			case UnitRole:
				return QVariant(static_cast<int>(unit));
		}
	}

	return QVariant();
}

CAmount DigitalNoteUnits::maxMoney()
{
	return MAX_SINGLE_TX;
}

