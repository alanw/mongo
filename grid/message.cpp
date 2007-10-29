/* message

   todo: authenticate; encrypt?
*/

#include "stdafx.h"
#include "message.h"

const int FragMax = 1480;
const int MSS = FragMax - 8;

#pragma pack(push)
#pragma pack(1)

struct Fragment {
	enum { MinFragmentLen = 8 + 1 };
	int msgId;
	short fragmentLen;
	short fragmentNo;
	char data[1];
	int fragmentDataLen() { return fragmentLen - 8; }

	bool ok(int nRead) { 
		if( nRead < MinFragmentLen || fragmentLen > nRead || fragmentLen < MinFragmentLen ) {
			cout << "recv: fragment bad. fragmentLen:" << fragmentLen << " nRead:" << nRead << endl;
			return false;
		}
		if( fragmentNo == 0 && fragmentLen < MinFragmentLen + MsgDataHeaderSize ) { 
			cout << "recv: bad first fragment. fragmentLen:" << fragmentLen << endl;
			return false;
		}
		return true;
	} 

	MsgData* startOfMsgData() { assert(fragmentNo == 0); return (MsgData *) data; }
};
#pragma pack(pop)

int NextMsgId = -1000;
struct MsgStart {
	MsgStart() {
		srand(3);
		NextMsgId = rand();
		assert(MsgDataHeaderSize == 20);
		assert(sizeof(Fragment) == 9);
	}
} msgstart;

MessagingPort::MessagingPort() {
}

MessagingPort::~MessagingPort() { 
}

void MessagingPort::init(int myUdpPort) {
	SockAddr me(myUdpPort);
	if( !conn.init(me) ) {
		cout << "conn init failure in MessagingPort::init " << myUdpPort << endl;
		exit(2);
	}
}

/* this is a temp implementation.  it will only work if there is a single entity messaging the receiver! */
bool MessagingPort::recv(Message& m) {
	int n = conn.recvfrom(buf, BufSize, m.from);
	Fragment *ff = (Fragment *) buf;
	if( !ff->ok(n) )
		return false;

	MsgData *somd = ff->startOfMsgData();
	int totalLen = somd->len;
	if( ff->fragmentDataLen() >= totalLen ) {
		// it's a short message, we got it all in one packet
		m.setData(somd, false);
		return true;
	}

	/* we'll need to read more */
	char *msgData = (char *) malloc(totalLen);
	char *p = msgData;
	memcpy(p, somd, ff->fragmentDataLen());
	int sofar = ff->fragmentDataLen();
	int wanted = totalLen;
	p += ff->fragmentDataLen();
	wanted -= ff->fragmentDataLen();

	/* note out of order, retransmits not done.  just get us going on localhost */
	int msgid = ff->msgId;
	int expectedFragmentNo = 1;
	SockAddr from;
	while( 1 ) {
		char b[FragMax];
		int n = conn.recvfrom(b, sizeof(b), from);
		Fragment *f = (Fragment *) b; 
		if( !f->ok(n) )
			return false;
		if( f->msgId != msgid || f->fragmentNo != expectedFragmentNo ) {
			cout << "bad fragment" << endl;
			return false;
		}
		if( from != m.from ) {
			cout << "wrong sender? impl not done for multiple 'clients'" << endl;
			assert(false);
			return false;
		}

		memcpy(p, f->startOfMsgData(), f->fragmentDataLen());
		p += f->fragmentDataLen();
		wanted -= f->fragmentDataLen();
		expectedFragmentNo++;

		if( wanted <= 0 ) {
			assert( wanted == 0 );
			break;
		}
	}

	return true;
}

void MessagingPort::reply(Message& received, Message& response) {
	say(received.from, response, received.data->id);
}

bool MessagingPort::call(SockAddr& to, Message& toSend, Message& response) {
	say(to, toSend);
	bool ok = recv(response);
	if( !ok )
		return false;
	assert( response.data->responseTo == toSend.data->id);
	return true;
}

void MessagingPort::say(SockAddr& to, Message& toSend, int responseTo) {
	toSend.data->reserved = 0;
	toSend.data->id = NextMsgId++;
	toSend.data->responseTo = responseTo;

	int left = toSend.data->len;
	assert( left > 0 && left <= 16 * 1024 * 1024 );
	Fragment *f = (Fragment *) buf;
	f->msgId = toSend.data->id;
	f->fragmentNo = 0;
	char *p = (char *) toSend.data;
	while( left > 0 ) { 
		int l = left > MSS ? MSS : left;
		f->fragmentLen = l + 8;
		memcpy(f->data, p, l);
		p += l;
		left -= l;
		conn.sendto(buf, l+8, to);
	}
}