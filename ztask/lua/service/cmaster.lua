local ztask = require "ztask"
require "ztask.manager"	-- import ztask.launch, ...
local socket = require "ztask.socket"

--[[
	master manage data :
		1. all the slaves address : id -> ipaddr:port
		2. all the global names : name -> address

	master hold connections from slaves .

	protocol slave->master :
		package size 1 byte
		type 1 byte :
			'H' : HANDSHAKE, report slave id, and address.
			'R' : REGISTER name address
			'Q' : QUERY name


	protocol master->slave:
		package size 1 byte
		type 1 byte :
			'W' : WAIT n
			'C' : CONNECT slave_id slave_address
			'N' : NAME globalname address
			'D' : DISCONNECT slave_id
]]

local slave_node = {}	--节点数组
local global_name = {}	--全局名称数组

local function read_package(fd)
	local sz = socket.read(fd, 1)
	assert(sz, "连接关闭")
	sz = string.byte(sz)
	local content = assert(socket.read(fd, sz), "连接关闭")
	return ztask.unpack(content)
end

local function pack_package(...)
	local message = ztask.packstring(...)
	local size = #message
	assert(size <= 255 , "too long")
	return string.char(size) .. message
end
--通知老节点连接新的节点
local function report_slave(fd, slave_id, slave_addr)
	local message = pack_package("C", slave_id, slave_addr)
	local n = 0 --节点数量
	for k,v in pairs(slave_node) do
		if v.fd ~= 0 then
			socket.write(v.fd, message) --下发地址
			n = n + 1
		end
	end
	--通知新节点老节点总数量
	socket.write(fd, pack_package("W", n))
end
--握手
local function handshake(fd)
	--读取节点通报的ID和地址
	local t, slave_id, slave_addr = read_package(fd)
	assert(t=='H', "Invalid handshake type " .. t)
	assert(slave_id ~= 0 , "Invalid slave id 0")
	--重复连入
	--if slave_node[slave_id] then
	--	error(string.format("Slave %d already register on %s", slave_id, slave_node[slave_id].addr))
	--end
	report_slave(fd, slave_id, slave_addr)
	slave_node[slave_id] = {
		fd = fd,
		id = slave_id,
		addr = slave_addr,
	}
	return slave_id , slave_addr
end
--处理节点消息
local function dispatch_slave(fd)
	local t, name, address = read_package(fd)
	if t == 'R' then
		-- 注册名字
		assert(type(address)=="number", "Invalid request")
		if not global_name[name] then
			global_name[name] = address
		end
		--通知所有节点注册了一个名字
		local message = pack_package("N", name, address)
		for k,v in pairs(slave_node) do
			socket.write(v.fd, message)
		end
	elseif t == 'Q' then
		-- 查询名字
		local address = global_name[name]
		if address then
			socket.write(fd, pack_package("N", name, address))
		end
	else
		ztask.error("Invalid slave message type " .. t)
	end
end

--监控节点
local function monitor_slave(slave_id, slave_address)
	local fd = slave_node[slave_id].fd
	ztask.error(string.format("Harbor %d (fd=%d) report %s", slave_id, fd, slave_address))
	--循环处理数据
	while pcall(dispatch_slave, fd) do end
	--连接断开则跳出循环
	ztask.error("节点 [ " ..slave_id .. " ] 掉线")
	--移除节点
	slave_node[slave_id] = nil
	--通知在线节点移除事件
	local message = pack_package("D", slave_id)
	for k,v in pairs(slave_node) do
		socket.write(v.fd, message)
	end
	socket.close(fd)
end

ztask.start(function()
	ztask.alias("中心协调服务")
	local master_addr = ztask.getenv "standalone"
	ztask.error("中心节点开始监听地址 : " .. tostring(master_addr))
	local fd = socket.listen(master_addr)
	socket.accept(fd , function(id, addr)
		--客户进入,开始接收数据
		ztask.error("连接来自:" .. addr .. " " .. id)
		socket.start(id)
		--开始握手
		local ok, slave, slave_addr = pcall(handshake, id)
		if ok then
			--握手成功
			ztask.fork(monitor_slave, slave, slave_addr)
		else
			--握手失败
			ztask.error(string.format("握手失败关闭句柄[ %d ], 错误[ %s ]", id, slave))
			socket.close(id)
		end
	end)
end)
