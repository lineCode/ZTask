local ztask = require "ztask"
local socket = require "ztask.socket"
local socketdriver = require "ztask.socketdriver"
require "ztask.manager"	-- import ztask.launch, ...
local table = table

local slaves = {}			--节点数组
local connect_queue = {}
local globalname = {}
local queryname = {}
local harbor = {}
local harbor_service
local monitor = {}
local monitor_master_set = {}

local function read_package(fd)
	local sz = socket.read(fd, 1)
	assert(sz, "closed")
	sz = string.byte(sz)
	local content = assert(socket.read(fd, sz), "closed")
	return ztask.unpack(content)
end

local function pack_package(...)
	local message = ztask.packstring(...)
	local size = #message
	assert(size <= 255 , "too long")
	return string.char(size) .. message
end

local function monitor_clear(id)
	local v = monitor[id]
	if v then
		monitor[id] = nil
		for _, v in ipairs(v) do
			v(true)
		end
	end
end
--连接节点
local function connect_slave(slave_id, address)
	ztask.error(string.format("开始连接节点 [ %d ],IP [%s]", slave_id, address))
	local ok, err = pcall(function()
		--if slaves[slave_id] == nil then --不对已连接过的服务做检查
			local fd = assert(socket.open(address), "Can't connect to "..address)
			socketdriver.nodelay(fd)
			socket.start(fd)
			ztask.error(string.format("连接到节点 [ %d ] (fd=%d),IP [%s]", slave_id, fd, address))
			slaves[slave_id] = fd
			monitor_clear(slave_id)
			socket.abandon(fd)
			ztask.send(harbor_service, "harbor", string.format("S %d %d",fd,slave_id))
		--end
	end)
	if not ok then
		ztask.error(err)
	end
end

local function ready()
	local queue = connect_queue
	connect_queue = nil
	for k,v in pairs(queue) do
		connect_slave(k,v)
	end
	for name,address in pairs(globalname) do
		ztask.redirect(harbor_service, address, "harbor", 0, "N " .. name)
	end
end

local function response_name(name)
	local address = globalname[name]
	if queryname[name] then
		local tmp = queryname[name]
		queryname[name] = nil
		for _,resp in ipairs(tmp) do
			resp(true, address)
		end
	end
end
--监控中心节点
local function monitor_master(master_fd)
	while true do
		local ok, t, id_name, address = pcall(read_package,master_fd)
		if ok then
			ztask.error("收到中心节点通知,类型 [ "..t.." ]")
			if t == 'C' then
				--连接节点
				if connect_queue then
					connect_queue[id_name] = address
				else
					connect_slave(id_name, address)
				end
			elseif t == 'N' then
				globalname[id_name] = address
				response_name(id_name)
				if connect_queue == nil then
					ztask.redirect(harbor_service, address, "harbor", 0, "N " .. id_name)
				end
			elseif t == 'D' then
				--节点移除
				local fd = slaves[id_name]
				slaves[id_name] = false
				if fd then
					monitor_clear(id_name)
					socket.close(fd)
				end
			end
		else
			ztask.error("中心节点断开")
			for _, v in ipairs(monitor_master_set) do
				v(true)
			end
			socket.close(master_fd)
			break
		end
	end
end
--接受节点连接
local function accept_slave(fd)
	socket.start(fd)
	--接收节点id
	local id = socket.read(fd, 1)
	if not id then
		--没收到节点id
		ztask.error(string.format("Connection (fd =%d) closed", fd))
		socket.close(fd)
		return
	end
	id = string.byte(id)
	if slaves[id] ~= nil then
		ztask.error(string.format("Slave %d exist (fd =%d)", id, fd))
		socket.close(fd)
		return
	end
	slaves[id] = fd
	monitor_clear(id)
	socket.abandon(fd)
	ztask.error(string.format("节点[ %d ]连接成功 (fd = %d)", id, fd))
	--通知港口服务添加一个连接
	ztask.send(harbor_service, "harbor", string.format("A %d %d", fd, id))
end

ztask.register_protocol {
	name = "harbor",
	id = ztask.PTYPE_HARBOR,
	pack = function(...) return ... end,
	unpack = ztask.tostring,
}

ztask.register_protocol {
	name = "text",
	id = ztask.PTYPE_TEXT,
	pack = function(...) return ... end,
	unpack = ztask.tostring,
}

local function monitor_harbor(master_fd)
	return function(session, source, command)
		local t = string.sub(command, 1, 1)
		local arg = string.sub(command, 3)
		if t == 'Q' then
			-- query name
			if globalname[arg] then
				ztask.redirect(harbor_service, globalname[arg], "harbor", 0, "N " .. arg)
			else
				socket.write(master_fd, pack_package("Q", arg))
			end
		elseif t == 'D' then
			-- harbor down
			local id = tonumber(arg)
			if slaves[id] then
				monitor_clear(id)
			end
			slaves[id] = false
		else
			ztask.error("Unknown command ", command)
		end
	end
end

function harbor.REGISTER(fd, name, handle)
	assert(globalname[name] == nil)
	globalname[name] = handle
	response_name(name)
	socket.write(fd, pack_package("R", name, handle))
	ztask.redirect(harbor_service, handle, "harbor", 0, "N " .. name)
end

function harbor.LINK(fd, id)
	if slaves[id] then
		if monitor[id] == nil then
			monitor[id] = {}
		end
		table.insert(monitor[id], ztask.response())
	else
		ztask.ret()
	end
end

function harbor.LINKMASTER()
	table.insert(monitor_master_set, ztask.response())
end

function harbor.CONNECT(fd, id)
	if not slaves[id] then
		if monitor[id] == nil then
			monitor[id] = {}
		end
		table.insert(monitor[id], ztask.response())
	else
		ztask.ret()
	end
end

function harbor.QUERYNAME(fd, name)
	if name:byte() == 46 then	-- "." , local name
		ztask.ret(ztask.pack(ztask.localname(name)))
		return
	end
	local result = globalname[name]
	if result then
		ztask.ret(ztask.pack(result))
		return
	end
	local queue = queryname[name]
	if queue == nil then
		socket.write(fd, pack_package("Q", name))
		queue = { ztask.response() }
		queryname[name] = queue
	else
		table.insert(queue, ztask.response())
	end
end

ztask.start(function()
	ztask.alias("节点服务")
	local master_addr = ztask.getenv "master"
	local harbor_id = tonumber(ztask.getenv "harbor")
	local slave_address = assert(ztask.getenv "address")
	local slave_fd = socket.listen(slave_address)
	ztask.error("连接中心节点: " .. tostring(master_addr))
	--连接master端
	local master_fd = assert(socket.open(master_addr), "未能连接到中心节点")
	socket.start(master_fd)

	ztask.dispatch("lua", function (_,_,command,...)
		local f = assert(harbor[command])
		f(master_fd, ...)
	end)
	ztask.dispatch("text", monitor_harbor(master_fd))
	--启动港口服务
	harbor_service = assert(ztask.launch("harbor", harbor_id, ztask.self()))
	--和master节点握手
	local hs_message = pack_package("H", harbor_id, slave_address)
	socket.write(master_fd, hs_message)
	--返回老节点数量
	local t, n = read_package(master_fd)
	assert(t == "W" and type(n) == "number", "slave shakehand failed")
	ztask.error(string.format("等待[ %d ]个节点连入", n))
	--开启协程监控mster节点的通知
	ztask.fork(monitor_master, master_fd)
	if n > 0 then
		local co = coroutine.running()
		socket.accept(slave_fd, function(fd, addr)
			ztask.error(string.format("新连接到达 (fd = %d, %s)",fd, addr))
			socketdriver.nodelay(fd)
			if pcall(accept_slave,fd) then
				--新节点连接成功
				local s = 0
				for k,v in pairs(slaves) do
					s = s + 1
				end
				if s >= n then
					ztask.wakeup(co)
				end
			end
		end)
		ztask.wait()
		--结束监听
		socket.close_fd(slave_fd)
	else
		-- 没有需要连接的节点.
		socket.close_fd(slave_fd)
	end
	ztask.error("节点连接完毕")
	ztask.fork(ready)
end)
