local driver = require "ztask.socketdriver"
local ztask = require "ztask"
local ztask_core = require "ztask.core"
local assert = assert

local socket = {}	-- api
local buffer_pool = {}	-- store all message buffer object
local socket_pool = setmetatable( -- store all socket object
	{},
	{ __gc = function(p)
		for id,v in pairs(p) do
			driver.close(id)
			-- don't need clear v.buffer, because buffer pool will be free at the end
			p[id] = nil
		end
	end
	}
)

local socket_message = {}

local function wakeup(s)
	local co = s.co
	if co then
		s.co = nil
		ztask.wakeup(co)
	end
end

local function suspend(s)
	assert(not s.co)
	s.co = coroutine.running()
	ztask.wait(s.co)
	-- wakeup closing corouting every time suspend,
	-- because socket.close() will wait last socket buffer operation before clear the buffer.
	if s.closing then
		ztask.wakeup(s.closing)
	end
end

-- read ztask_socket.h for these macro
-- SKYNET_SOCKET_TYPE_DATA = 1
socket_message[1] = function(id, size, data)
	local s = socket_pool[id]
	if s == nil then
		ztask.error("socket: drop package from " .. id)
		driver.drop(data, size)
		return
	end

	local sz = driver.push(s.buffer, buffer_pool, data, size)
	local rr = s.read_required
	local rrt = type(rr)
	if rrt == "number" then
		-- read size
		if sz >= rr then
			s.read_required = nil
			wakeup(s)
		end
	else
		if s.buffer_limit and sz > s.buffer_limit then
			ztask.error(string.format("socket buffer overflow: fd=%d size=%d", id , sz))
			driver.clear(s.buffer,buffer_pool)
			driver.close(id)
			return
		end
		if rrt == "string" then
			-- read line
			if driver.readline(s.buffer,nil,rr) then
				s.read_required = nil
				wakeup(s)
			end
		end
	end
end

-- SKYNET_SOCKET_TYPE_CLOSE = 4
socket_message[4] = function(id)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	s.connected = false
	wakeup(s)
end

-- SKYNET_SOCKET_TYPE_ACCEPT = 5
socket_message[5] = function(id, newid, addr)
	local s = socket_pool[id]
	if s == nil then
		driver.close(newid)
		return
	end
	s.callback(newid, addr)
end

-- SKYNET_SOCKET_TYPE_ERROR = 6
socket_message[6] = function(id, _, err)
	local s = socket_pool[id]
	if s == nil then
		ztask.error("套接字: 未知错误来自", id, err)
		return
	end
	if s.connected then
		ztask.error("套接字: 错误来自", id, err)
	elseif s.connecting then
		s.connecting = err
	end
	s.connected = false
	driver.shutdown(id)

	wakeup(s)
end

-- SKYNET_SOCKET_TYPE_UDP = 7
socket_message[7] = function(id, size, data, address)
	local s = socket_pool[id]
	if s == nil or s.callback == nil then
		ztask.error("socket: drop udp package from " .. id)
		driver.drop(data, size)
		return
	end
	local str = ztask.tostring(data, size)
	ztask_core.trash(data, size)
	s.callback(str, address)
end

local function default_warning(id, size)
	local s = socket_pool[id]
	if not s then
		return
	end
	ztask.error(string.format("WARNING: %d K bytes need to send out (fd = %d)", size, id))
end

-- SKYNET_SOCKET_TYPE_WARNING
socket_message[8] = function(id, size)
	local s = socket_pool[id]
	if s then
		local warning = s.on_warning or default_warning
		warning(id, size)
	end
end

ztask.register_protocol {
	name = "socket",
	id = ztask.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
	unpack = driver.unpack,
	dispatch = function (_, _, t, ...)
		socket_message[t](...)
	end
}

local function connect(id, func)
	local newbuffer
	if func == nil then
		newbuffer = driver.buffer()
	end
	local s = {
		id = id,
		buffer = newbuffer,
		connected = false,
		connecting = true,
		read_required = false,
		co = false,
		callback = func,
		protocol = "TCP",
	}
	assert(not socket_pool[id], "socket is not closed")
	socket_pool[id] = s
	suspend(s)
	local err = s.connecting
	s.connecting = nil
	if s.connected then
		return id
	else
		socket_pool[id] = nil
		return nil, err
	end
end
--打开地址
function socket.open(addr, port)
	local session = ztask.genid()
	--提交请求
	driver.connect(addr,port,session)
	--等待返回
	local msg,sz = ztask.yield(session)
	--处理数据
	local type, id, size, data = driver.unpack(msg,sz)
	if type == 2 then -- ZTASK_SOCKET_TYPE_CONNECT
		return id
	else
		return nil
	end
end

function socket.bind(os_fd)
	local id = driver.bind(os_fd)
	return connect(id)
end

function socket.stdin()
	return socket.bind(0)
end
--开始接收数据
function socket.start(id)
	local session = ztask.genid()
	--提交请求
	driver.start(id, session)
	--等待返回
	local msg,sz = ztask.yield(session)
	--处理数据
	local type, id, size, data = driver.unpack(msg,sz)
	if type == 3 then -- ZTASK_SOCKET_TYPE_START
		--记录句柄
		local newbuffer = driver.buffer()
		local s = {
			id = id,
			buffer = newbuffer,
			connected = true,
			connecting = nil,
			read_required = false,
			co = false,
			callback = nil,
			protocol = "TCP",
		}
		assert(not socket_pool[id], "socket is not closed")
		socket_pool[id] = s
		return id
	else
		return nil
	end
end
--开始接收连接,在回调函数响应
function socket.accept(id, func)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	s.callback = func
	local session = ztask.genid()
	--提交请求
	driver.accept(id, session)
	--等待返回
	local msg,sz = ztask.yield(session)
	--处理数据
	local type, id, size, data = driver.unpack(msg,sz)
	if type == 3 then -- ZTASK_SOCKET_TYPE_START
		return id
	else
		return nil
	end
end

function socket.shutdown(id)
	local s = socket_pool[id]
	if s then
		driver.clear(s.buffer,buffer_pool)
		-- the framework would send SKYNET_SOCKET_TYPE_CLOSE , need close(id) later
		driver.shutdown(id)
	end
end

function socket.close_fd(id)
	socket_pool[id] = nil
	driver.close(id)
end

function socket.close(id)
	local s = socket_pool[id]
	if s == nil then
		return
	end
	if s.connected then
		driver.close(id)
		-- notice: call socket.close in __gc should be carefully,
		-- because ztask.wait never return in __gc, so driver.clear may not be called
		if s.co then
			-- reading this socket on another coroutine, so don't shutdown (clear the buffer) immediately
			-- wait reading coroutine read the buffer.
			assert(not s.closing)
			s.closing = coroutine.running()
			ztask.wait(s.closing)
		else
			suspend(s)
		end
		s.connected = false
	end
	driver.clear(s.buffer,buffer_pool)
	assert(s.lock == nil or next(s.lock) == nil)
	socket_pool[id] = nil
end

function socket.read(id, sz)
	local s = socket_pool[id]
	assert(s)
	if sz == nil then
		-- read some bytes
		local ret = driver.readall(s.buffer, buffer_pool)
		if ret ~= "" then
			return ret
		end

		if not s.connected then
			return false, ret
		end
		assert(not s.read_required)
		s.read_required = 0
		suspend(s)
		ret = driver.readall(s.buffer, buffer_pool)
		if ret ~= "" then
			return ret
		else
			return false, ret
		end
	end

	local ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	end
	if not s.connected then
		return false, driver.readall(s.buffer, buffer_pool)
	end

	assert(not s.read_required)
	s.read_required = sz
	suspend(s)
	ret = driver.pop(s.buffer, buffer_pool, sz)
	if ret then
		return ret
	else
		return false, driver.readall(s.buffer, buffer_pool)
	end
end

function socket.readall(id)
	local s = socket_pool[id]
	assert(s)
	if not s.connected then
		local r = driver.readall(s.buffer, buffer_pool)
		return r ~= "" and r
	end
	assert(not s.read_required)
	s.read_required = true
	suspend(s)
	assert(s.connected == false)
	return driver.readall(s.buffer, buffer_pool)
end

function socket.readline(id, sep)
	sep = sep or "\n"
	local s = socket_pool[id]
	assert(s)
	local ret = driver.readline(s.buffer, buffer_pool, sep)
	if ret then
		return ret
	end
	if not s.connected then
		return false, driver.readall(s.buffer, buffer_pool)
	end
	assert(not s.read_required)
	s.read_required = sep
	suspend(s)
	if s.connected then
		return driver.readline(s.buffer, buffer_pool, sep)
	else
		return false, driver.readall(s.buffer, buffer_pool)
	end
end

function socket.block(id)
	local s = socket_pool[id]
	if not s or not s.connected then
		return false
	end
	assert(not s.read_required)
	s.read_required = 0
	suspend(s)
	return s.connected
end

socket.write = assert(driver.send)
socket.lwrite = assert(driver.lsend)
socket.header = assert(driver.header)

function socket.invalid(id)
	return socket_pool[id] == nil
end

function socket.disconnected(id)
	local s = socket_pool[id]
	if s then
		return not(s.connected or s.connecting)
	end
end

function socket.listen(host, port, backlog)
	if port == nil then
		host, port = string.match(host, "([^:]+):(.+)$")
		port = tonumber(port)
	end
	local session = ztask.genid()
	--提交请求
	driver.listen(host, port, backlog, session)
	--等待返回
	local msg,sz = ztask.yield(session)
	--处理数据
	local type, id, size, data = driver.unpack(msg,sz)
	if type == 2 then -- ZTASK_SOCKET_TYPE_CONNECT
		--记录句柄
		local s = {
			id = id,
			buffer = nil,
			connected = true,
			connecting = nil,
			read_required = false,
			co = false,
			callback = nil,
			protocol = "TCP",
		}
		assert(not socket_pool[id], "socket is not closed")
		socket_pool[id] = s
		return id
	else
		return nil
	end
end

function socket.lock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = s.lock
	if not lock_set then
		lock_set = {}
		s.lock = lock_set
	end
	if #lock_set == 0 then
		lock_set[1] = true
	else
		local co = coroutine.running()
		table.insert(lock_set, co)
		ztask.wait(co)
	end
end

function socket.unlock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = assert(s.lock)
	table.remove(lock_set,1)
	local co = lock_set[1]
	if co then
		ztask.wakeup(co)
	end
end

-- abandon use to forward socket id to other service
-- you must call socket.start(id) later in other service
function socket.abandon(id)
	local s = socket_pool[id]
	if s then
		driver.clear(s.buffer,buffer_pool)
		s.connected = false
		wakeup(s)
		socket_pool[id] = nil
	end
end

function socket.limit(id, limit)
	local s = assert(socket_pool[id])
	s.buffer_limit = limit
end

---------------------- UDP

local function create_udp_object(id, cb)
	assert(not socket_pool[id], "socket is not closed")
	socket_pool[id] = {
		id = id,
		connected = true,
		protocol = "UDP",
		callback = cb,
	}
end

function socket.udp(callback, host, port)
	local id = driver.udp(host, port)
	create_udp_object(id, callback)
	return id
end

function socket.udp_connect(id, addr, port, callback)
	local obj = socket_pool[id]
	if obj then
		assert(obj.protocol == "UDP")
		if callback then
			obj.callback = callback
		end
	else
		create_udp_object(id, callback)
	end
	driver.udp_connect(id, addr, port)
end

socket.sendto = assert(driver.udp_send)
socket.udp_address = assert(driver.udp_address)

function socket.warning(id, callback)
	local obj = socket_pool[id]
	assert(obj)
	obj.on_warning = callback
end

return socket
