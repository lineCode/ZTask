local ztask = require "ztask"
local harbor = require "ztask.harbor"
require "ztask.manager"
local memory = require "ztask.memory"

ztask.start(function()
	ztask.alias("LUA自举")
	
	local sharestring = tonumber(ztask.getenv "sharestring" or 4096)
	memory.ssexpand(sharestring)
	--获取master节点配置
	local standalone = ztask.getenv "standalone"
	--获取节点ID
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
		--如果是master节点
		if standalone then
			if not pcall(ztask.newservice,"cmaster") then
				ztask.abort()
			end
		end
		--启动slave节点
		local ok, slave = pcall(ztask.newservice, "cslave")
		if not ok then
			ztask.abort()
		end
		ztask.name(".cslave", slave)
	end
	--为中心节点启动数据服务
	if standalone then
		local datacenter = ztask.newservice "datacenterd"
		ztask.name("DATACENTER", datacenter)
	end
	--启动服务管理器
	ztask.newservice("service_mgr")
	--启动调试器
	local debug = ztask.getenv "debug"
	if debug then
		local host, port = string.match(debug, "([^:]+):(.+)$")
		port = tonumber(port)
		pcall(ztask.newservice, "debug_console", host, port)
	end
	--启动入口服务
	local start = ztask.getenv "start"
	if start then
		pcall(ztask.newservice, start)
	end
	--退出服务
	ztask.exit()
end)
