local ztask = require "ztask"
require "ztask.manager"	-- import ztask.register

local cmd = {}
local service = {}

local function request(name, func, ...)
	local ok, handle = pcall(func, ...)
	local s = service[name]
	assert(type(s) == "table")
	if ok then
		service[name] = handle
	else
		service[name] = tostring(handle)
	end

	for _,v in ipairs(s) do
		ztask.wakeup(v)
	end

	if ok then
		return handle
	else
		error(tostring(handle))
	end
end

local function waitfor(name , func, ...)
	local s = service[name]
	if type(s) == "number" then
		return s
	end
	local co = coroutine.running()

	if s == nil then
		s = {}
		service[name] = s
	elseif type(s) == "string" then
		error(s)
	end

	assert(type(s) == "table")

	if not s.launch and func then
		s.launch = true
		return request(name, func, ...)
	end

	table.insert(s, co)
	ztask.wait()
	s = service[name]
	if type(s) == "string" then
		error(s)
	end
	assert(type(s) == "number")
	return s
end

local function read_name(service_name)
	if string.byte(service_name) == 64 then -- '@'
		return string.sub(service_name , 2)
	else
		return service_name
	end
end

function cmd.LAUNCH(service_name, subname, ...)
	local realname = read_name(service_name)
	return waitfor(service_name, ztask.newservice, realname, subname, ...)
end

function cmd.QUERY(service_name, subname)
	local realname = read_name(service_name)
	return waitfor(service_name)
end

local function list_service()
	local result = {}
	for k,v in pairs(service) do
		if type(v) == "string" then
			v = "Error: " .. v
		elseif type(v) == "table" then
			v = "Querying"
		else
			v = ztask.address(v)
		end

		result[k] = v
	end

	return result
end


local function register_global()
	function cmd.GLAUNCH(name, ...)
		local global_name = "@" .. name
		return cmd.LAUNCH(global_name, ...)
	end

	function cmd.GQUERY(name, ...)
		local global_name = "@" .. name
		return cmd.QUERY(global_name, ...)
	end

	local mgr = {}

	function cmd.REPORT(m)
		mgr[m] = true
	end

	local function add_list(all, m)
		local harbor = "@" .. ztask.harbor(m)
		local result = ztask.call(m, "lua", "LIST")
		for k,v in pairs(result) do
			all[k .. harbor] = v
		end
	end

	function cmd.LIST()
		local result = {}
		for k in pairs(mgr) do
			pcall(add_list, result, k)
		end
		local l = list_service()
		for k, v in pairs(l) do
			result[k] = v
		end
		return result
	end
end

local function register_local()
	local function waitfor_remote(cmd, name, ...)
		local global_name = "@" .. name
		local local_name
		local_name = global_name
		return waitfor(local_name, ztask.call, "SERVICE", "lua", cmd, global_name, ...)
	end

	function cmd.GLAUNCH(...)
		return waitfor_remote("LAUNCH", ...)
	end

	function cmd.GQUERY(...)
		return waitfor_remote("QUERY", ...)
	end

	function cmd.LIST()
		return list_service()
	end

	ztask.call("SERVICE", "lua", "REPORT", ztask.self())
end

ztask.start(function()
	ztask.alias("LUA服务管理")
	ztask.dispatch("lua", function(session, address, command, ...)
		local f = cmd[command]
		if f == nil then
			ztask.ret(ztask.pack(nil, "Invalid command " .. command))
			return
		end

		local ok, r = pcall(f, ...)

		if ok then
			ztask.ret(ztask.pack(r))
		else
			ztask.ret(ztask.pack(nil, r))
		end
	end)
	local handle = ztask.localname ".service"
	if  handle then
		ztask.error(".service is already register by ", ztask.address(handle))
		ztask.exit()
	else
		ztask.register(".service")
	end
	if ztask.getenv "standalone" then
		--如果是中心节点则注册全局名称
		ztask.register("SERVICE")
		register_global()
	else
		register_local()
	end
end)
