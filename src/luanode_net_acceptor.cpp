#include "StdAfx.h"
#include "luanode.h"
#include "luanode_net.h"
#include "luanode_net_acceptor.h"
#include "blogger.h"

#include <boost/asio/placeholders.hpp>

#include <boost/bind.hpp>

using namespace LuaNode::Net;

static unsigned long s_nextAcceptorId = 0;
static unsigned long s_acceptorCount = 0;

//////////////////////////////////////////////////////////////////////////
/// 
void LuaNode::Net::Acceptor::RegisterFunctions(lua_State* L) {}


const char* Acceptor::className = "Acceptor";
const Acceptor::RegType Acceptor::methods[] = {
	{"open", &Acceptor::Open},
	{"close", &Acceptor::Close},
	{"setoption", &Acceptor::SetOption},
	{"getsockname", &Acceptor::GetLocalAddress},
	{"bind", &Acceptor::Bind},
	{"listen", &Acceptor::Listen},
	{"accept", &Acceptor::Accept},
	{0}
};

const Acceptor::RegType Acceptor::setters[] = {
	{0}
};

const Acceptor::RegType Acceptor::getters[] = {
	{0}
};

Acceptor::Acceptor(lua_State* L) : 
	m_L(L),
	m_acceptor( GetIoService() ),
	m_acceptorId(++s_nextAcceptorId)
{
	s_acceptorCount++;
	LogDebug("Constructing Acceptor (%p) (id=%d). Current acceptor count = %d", this, m_acceptorId, s_acceptorCount);
}

Acceptor::~Acceptor(void)
{
	s_acceptorCount--;
	LogDebug("Destructing Acceptor (%p) (id=%d). Current acceptor count = %d", this, m_acceptorId, s_acceptorCount);
}

//////////////////////////////////////////////////////////////////////////
/// 
/*static*/ int Acceptor::tostring_T(lua_State* L) {
	userdataType* ud = static_cast<userdataType*>(lua_touserdata(L, 1));
	Acceptor* obj = ud->pT;
	lua_pushfstring(L, "%s (%p) (id=%d)", className, obj, obj->m_acceptorId);
	return 1;
}

//////////////////////////////////////////////////////////////////////////
/// Open the acceptor using the specified protocol
int Acceptor::Open(lua_State* L) {
	const char* kind = luaL_checkstring(L, 2);
	LogDebug("Acceptor::Open (%p) (id=%d) - %s", this, m_acceptorId, kind);

	boost::system::error_code ec;
	if(strcmp(kind, "tcp4") == 0) {
		m_acceptor.open( boost::asio::ip::tcp::v4(), ec );
	}
	else if(strcmp(kind, "tcp6") == 0) {
		m_acceptor.open( boost::asio::ip::tcp::v6(), ec );
	}
	return BoostErrorCodeToLua(L, ec);
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::Close(lua_State* L) {
	LogDebug("Acceptor::Close (%p) (id=%d)", this, m_acceptorId);
	boost::system::error_code ec;
	m_acceptor.close(ec);
	return BoostErrorCodeToLua(L, ec);
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::SetOption(lua_State* L) {
	const char* option = luaL_checkstring(L, 2);
	LogDebug("Acceptor::SetOption (%p) (id=%d) - %s", this, m_acceptorId, option);

	if(strcmp(option, "reuseaddr") == 0) {
		bool value = lua_toboolean(L, 3) != 0;
		m_acceptor.set_option( boost::asio::socket_base::reuse_address(value) );
	}
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::GetLocalAddress(lua_State* L) {
	const boost::asio::ip::tcp::endpoint& endpoint = m_acceptor.local_endpoint();

	lua_pushstring(L, endpoint.address().to_string().c_str());
	lua_pushinteger(L, endpoint.port());
	return 2;
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::Bind(lua_State* L) {
	const char* ip = luaL_checkstring(L, 2);
	unsigned short port = luaL_checkinteger(L, 3);

	LogDebug("Acceptor::Bind (%p) (id=%d) - (%s,%d)", this, m_acceptorId, ip, port);

	boost::system::error_code ec;
	boost::asio::ip::address address = boost::asio::ip::address::from_string(ip, ec);
	if(ec) {
		return BoostErrorCodeToLua(L, ec);
	}

	boost::asio::ip::tcp::endpoint endpoint( address, port );
	m_acceptor.bind(endpoint, ec);
	return BoostErrorCodeToLua(L, ec);
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::Listen(lua_State* L) {
	int backlog = luaL_optinteger(L, 2, boost::asio::socket_base::max_connections);
	LogDebug("Acceptor::Listen (%p) (id=%d) - backlog = %d", this, m_acceptorId, backlog);

	boost::system::error_code ec;
	m_acceptor.listen(backlog, ec);
	return BoostErrorCodeToLua(L, ec);
}

//////////////////////////////////////////////////////////////////////////
/// 
int Acceptor::Accept(lua_State* L) {
	LogDebug("Acceptor::Accept (%p) (id=%d)", this, m_acceptorId);

	boost::asio::ip::tcp::socket* socket = new boost::asio::ip::tcp::socket( GetIoService() );

	// store a reference in the registry
	lua_pushvalue(L, 1);
	int reference = luaL_ref(L, LUA_REGISTRYINDEX);

	m_acceptor.async_accept(
		*socket,
		boost::bind(&Acceptor::HandleAccept, this, reference, socket, boost::asio::placeholders::error)
	);
	return 0;
}

//////////////////////////////////////////////////////////////////////////
/// 
void Acceptor::HandleAccept(int reference, boost::asio::ip::tcp::socket* socket, const boost::system::error_code& error) {
	LogDebug("Acceptor::HandleAccept (%p) (id=%d) (new socket %p)", this, m_acceptorId, socket);
	lua_State* L = m_L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, reference);
	luaL_unref(L, LUA_REGISTRYINDEX, reference);

	if(!error) {
		boost::asio::ip::address address = socket->remote_endpoint().address();
		//OnConnectionEstablished(socket, address);
		lua_getfield(L, 1, "callback");
		if(lua_type(L, 2) == LUA_TFUNCTION) {
			lua_newtable(L);
			int peer = lua_gettop(L);

			lua_pushstring(L, "socket");
			Socket* luasocket = new Socket(L, socket);
			Socket::push(L, luasocket, true);	// now Socket is the owner
			lua_rawset(L, peer);

			const std::string& sAddress = address.to_string();
			lua_pushstring(L, "address");
			lua_pushlstring(L, sAddress.c_str(), sAddress.length());
			lua_rawset(L, peer);

			lua_pushstring(L, "port");
			lua_pushnumber(L, socket->remote_endpoint().port());
			lua_rawset(L, peer);

			LuaNode::GetLuaVM().call(1, LUA_MULTRET);
		}
		else {
			// do nothing?
		}
	}
	else {
		if(error != boost::asio::error::operation_aborted) {
			LogError("Acceptor::HandleAccept (%p) (id=%d) (new socket %p) - %s", this, m_acceptorId, socket, error.message().c_str());
		}
		////Pm_allocator.ReleaseSocket(socket);
		// we don't call OnSocketReleased, because a connection had not been established
	}
	lua_settop(L, 0);
}
