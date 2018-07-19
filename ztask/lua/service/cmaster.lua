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

local slave_node = {}	--�ڵ�����
local global_name = {}	--ȫ����������

local function read_package(fd)
	local sz = socket.read(fd, 1)
	assert(sz, "���ӹر�")
	sz = string.byte(sz)
	local content = assert(socket.read(fd, sz), "���ӹر�")
	return ztask.unpack(content)
end

local function pack_package(...)
	local message = ztask.packstring(...)
	local size = #message
	assert(size <= 255 , "too long")
	return string.char(size) .. message
end
--֪ͨ�Ͻڵ������µĽڵ�
local function report_slave(fd, slave_id, slave_addr)
	local message = pack_package("C", slave_id, slave_addr)
	local n = 0 --�ڵ�����
	for k,v in pairs(slave_node) do
		if v.fd ~= 0 then
			socket.write(v.fd, message) --�·���ַ
			n = n + 1
		end
	end
	--֪ͨ�½ڵ��Ͻڵ�������
	socket.write(fd, pack_package("W", n))
end
--����
local function handshake(fd)
	--��ȡ�ڵ�ͨ����ID�͵�ַ
	local t, slave_id, slave_addr = read_package(fd)
	assert(t=='H', "Invalid handshake type " .. t)
	assert(slave_id ~= 0 , "Invalid slave id 0")
	--�ظ�����
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
--����ڵ���Ϣ
local function dispatch_slave(fd)
	local t, name, address = read_package(fd)
	if t == 'R' then
		-- ע������
		assert(type(address)=="number", "Invalid request")
		if not global_name[name] then
			global_name[name] = address
		end
		--֪ͨ���нڵ�ע����һ������
		local message = pack_package("N", name, address)
		for k,v in pairs(slave_node) do
			socket.write(v.fd, message)
		end
	elseif t == 'Q' then
		-- ��ѯ����
		local address = global_name[name]
		if address then
			socket.write(fd, pack_package("N", name, address))
		end
	else
		ztask.error("Invalid slave message type " .. t)
	end
end

--��ؽڵ�
local function monitor_slave(slave_id, slave_address)
	local fd = slave_node[slave_id].fd
	ztask.error(string.format("Harbor %d (fd=%d) report %s", slave_id, fd, slave_address))
	--ѭ����������
	while pcall(dispatch_slave, fd) do end
	--���ӶϿ�������ѭ��
	ztask.error("�ڵ� [ " ..slave_id .. " ] ����")
	--�Ƴ��ڵ�
	slave_node[slave_id] = nil
	--֪ͨ���߽ڵ��Ƴ��¼�
	local message = pack_package("D", slave_id)
	for k,v in pairs(slave_node) do
		socket.write(v.fd, message)
	end
	socket.close(fd)
end

ztask.start(function()
	ztask.alias("����Э������")
	local master_addr = ztask.getenv "standalone"
	ztask.error("���Ľڵ㿪ʼ������ַ : " .. tostring(master_addr))
	local fd = socket.listen(master_addr)
	socket.accept(fd , function(id, addr)
		--�ͻ�����,��ʼ��������
		ztask.error("��������:" .. addr .. " " .. id)
		socket.start(id)
		--��ʼ����
		local ok, slave, slave_addr = pcall(handshake, id)
		if ok then
			--���ֳɹ�
			ztask.fork(monitor_slave, slave, slave_addr)
		else
			--����ʧ��
			ztask.error(string.format("����ʧ�ܹرվ��[ %d ], ����[ %s ]", id, slave))
			socket.close(id)
		end
	end)
end)
