local ztask = require "ztask"
local harbor = require "ztask.harbor"
require "ztask.manager"
local memory = require "ztask.memory"

ztask.start(function()
	ztask.alias("LUA�Ծ�")
	
	local sharestring = tonumber(ztask.getenv "sharestring" or 4096)
	memory.ssexpand(sharestring)
	--��ȡmaster�ڵ�����
	local standalone = ztask.getenv "standalone"
	--��ȡ�ڵ�ID
	local harbor_id = tonumber(ztask.getenv "harbor" or 0)
	if harbor_id == 0 then
		assert(standalone ==  nil)
		standalone = true
		ztask.setenv("standalone", "true")

		local ok, slave = pcall(ztask.newservice, "cdummy")
		if not ok then
			ztask.abort()
		end
		ztask.name(".cslave", slave)
	else
		--�����master�ڵ�
		if standalone then
			if not pcall(ztask.newservice,"cmaster") then
				ztask.abort()
			end
		end
		--����slave�ڵ�
		local ok, slave = pcall(ztask.newservice, "cslave")
		if not ok then
			ztask.abort()
		end
		ztask.name(".cslave", slave)
	end
	--Ϊ���Ľڵ��������ݷ���
	if standalone then
		local datacenter = ztask.newservice "datacenterd"
		ztask.name("DATACENTER", datacenter)
	end
	--�������������
	ztask.newservice("service_mgr")
	--����������
	local debug = ztask.getenv "debug"
	if debug then
		local host, port = string.match(debug, "([^:]+):(.+)$")
		port = tonumber(port)
		pcall(ztask.newservice, "debug_console", host, port)
	end
	--������ڷ���
	local start = ztask.getenv "start"
	if start then
		pcall(ztask.newservice, start)
	end
	--�˳�����
	ztask.exit()
end)
