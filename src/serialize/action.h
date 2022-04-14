#ifndef SERIALIZE_ACTION_H
#define SERIALIZE_ACTION_H

class CSerActionGetSerializeSize
{
	
};

struct CSerActionSerialize
{
	bool ForRead() const
	{
		return false;
	}
};

struct CSerActionUnserialize
{
	bool ForRead() const
	{
		return true;
	}
};

#endif // SERIALIZE_ACTION_H
