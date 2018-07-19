local c = require "ztask.core"
local tostring = tostring
local tonumber = tonumber
local coroutine = coroutine
local assert = assert
local pairs = pairs
local pcall = pcall
local table = table

local profile = require "profile"

local coroutine_resume = profile.resume
local coroutine_yield = profile.yield

local proto = {}
local ztask = {
	-- read ztask.h
	PTYPE_TEXT = 0,
	PTYPE_RESPONSE = 1,
	PTYPE_MULTICAST = 2,
	PTYPE_CLIENT = 3,
	PTYPE_SYSTEM = 4,
	PTYPE_HARBOR = 5,
	PTYPE_SOCKET = 6,
	PTYPE_ERROR = 7,
	PTYPE_QUEUE = 8,	-- used in deprecated mqueue, use ztask.queue instead
	PTYPE_DEBUG = 9,
	PTYPE_LUA = 10,
	PTYPE_SNAX = 11,
}

-- code cache
ztask.cache = require "ztask.codecache"

function ztask.register_protocol(class)
	local name = class.name
	local id = class.id
	assert(proto[name] == nil and proto[id] == nil)
	assert(type(name) == "string" and type(id) == "number" and id >=0 and id <=255)
	proto[name] = class
	proto[id] = class
end

local session_id_coroutine = {}
local session_coroutine_id = {}
local session_coroutine_address = {}
local session_response = {}
local unresponse = {}

local wakeup_queue = {}
local sleep_session = {}

local watching_service = {}
local watching_session = {}
local dead_service = {}
local error_queue = {}
local fork_queue = {}

-- suspend is function
local suspend

local function string_to_handle(str)
	return tonumber("0x" .. string.sub(str , 2))
end

----- monitor exit

local function dispatch_error_queue()
	local session = table.remove(error_queue,1)
	if session then
		local co = session_id_coroutine[session]
		session_id_coroutine[session] = nil
		return suspend(co, coroutine_resume(co, false))
	end
end

local function _error_dispatch(error_session, error_source)
	if error_session == 0 then
		-- service is down
		--  Don't remove from watching_service , because user may call dead service
		if watching_service[error_source] then
			dead_service[error_source] = true
		end
		for session, srv in pairs(watching_session) do
			if srv == error_source then
				table.insert(error_queue, session)
			end
		end
	else
		-- capture an error for error_session
		if watching_session[error_session] then
			table.insert(error_queue, error_session)
		end
	end
end

-- coroutine reuse

local coroutine_pool = setmetatable({}, { __mode = "kv" })

local function co_create(f)
	local co = table.remove(coroutine_pool)
	if co == nil then
		co = coroutine.create(function(...)
			f(...)
			while true do
				f = nil
				coroutine_pool[#coroutine_pool+1] = co
				f = coroutine_yield "EXIT"
				f(coroutine_yield())
			end
		end)
	else
		coroutine_resume(co, f)
	end
	return co
end

local function dispatch_wakeup()
	local co = table.remove(wakeup_queue,1)
	if co then
		local session = sleep_session[co]
		if session then
			session_id_coroutine[session] = "BREAK"
			return suspend(co, coroutine_resume(co, false, "BREAK"))
		end
	end
end

local function release_watching(address)
	local ref = watching_service[address]
	if ref then
		ref = ref - 1
		if ref > 0 then
			watching_service[address] = ref
		else
			watching_service[address] = nil
		end
	end
end

-- suspend is local function
function suspend(co, result, command, param, size)
	if not result then
		local session = session_coroutine_id[co]
		if session then -- coroutine may fork by others (session is nil)
			local addr = session_coroutine_address[co]
			if session ~= 0 then
				-- only call response error
				c.send(addr, ztask.PTYPE_ERROR, session, "")
			end
			session_coroutine_id[co] = nil
			session_coroutine_address[co] = nil
		end
		error(debug.traceback(co,tostring(command)))
	end
	if command == "CALL" then
		session_id_coroutine[param] = co
	elseif command == "SLEEP" then
		session_id_coroutine[param] = co
		sleep_session[co] = param
	elseif command == "RETURN" then
		local co_session = session_coroutine_id[co]
		if co_session == 0 then
			if size ~= nil then
				c.trash(param, size)
			end
			return suspend(co, coroutine_resume(co, false))	-- send don't need ret
		end
		local co_address = session_coroutine_address[co]
		if param == nil or session_response[co] then
			error(debug.traceback(co))
		end
		session_response[co] = true
		local ret
		if not dead_service[co_address] then
			ret = c.send(co_address, ztask.PTYPE_RESPONSE, co_session, param, size) ~= nil
			if not ret then
				-- If the package is too large, returns nil. so we should report error back
				c.send(co_address, ztask.PTYPE_ERROR, co_session, "")
			end
		elseif size ~= nil then
			c.trash(param, size)
			ret = false
		end
		return suspend(co, coroutine_resume(co, ret))
	elseif command == "RESPONSE" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		if session_response[co] then
			error(debug.traceback(co))
		end
		local f = param
		local function response(ok, ...)
			if ok == "TEST" then
				if dead_service[co_address] then
					release_watching(co_address)
					unresponse[response] = nil
					f = false
					return false
				else
					return true
				end
			end
			if not f then
				if f == false then
					f = nil
					return false
				end
				error "Can't response more than once"
			end

			local ret
			-- do not response when session == 0 (send)
			if co_session ~= 0 and not dead_service[co_address] then
				if ok then
					ret = c.send(co_address, ztask.PTYPE_RESPONSE, co_session, f(...)) ~= nil
					if not ret then
						-- If the package is too large, returns false. so we should report error back
						c.send(co_address, ztask.PTYPE_ERROR, co_session, "")
					end
				else
					ret = c.send(co_address, ztask.PTYPE_ERROR, co_session, "") ~= nil
				end
			else
				ret = false
			end
			release_watching(co_address)
			unresponse[response] = nil
			f = nil
			return ret
		end
		watching_service[co_address] = watching_service[co_address] + 1
		session_response[co] = true
		unresponse[response] = true
		return suspend(co, coroutine_resume(co, response))
	elseif command == "EXIT" then
		-- coroutine exit
		local address = session_coroutine_address[co]
		if address then
			release_watching(address)
			session_coroutine_id[co] = nil
			session_coroutine_address[co] = nil
			session_response[co] = nil
		end
	elseif command == "QUIT" then
		-- service exit
		return
	elseif command == "USER" then
		-- See ztask.coutine for detail
		error("Call ztask.coroutine.yield out of ztask.coroutine.resume\n" .. debug.traceback(co))
	elseif command == nil then
		-- debug trace
		return
	else
		error("Unknown command : " .. command .. "\n" .. debug.traceback(co))
	end
	dispatch_wakeup()
	dispatch_error_queue()
end
--定时器
function ztask.timeout(ti, func)
	local session = c.intcommand("TIMEOUT",ti)
	assert(session)
	local co = co_create(func)
	assert(session_id_coroutine[session] == nil)
	session_id_coroutine[session] = co
end
--延迟
function ztask.sleep(ti)
	local session = c.intcommand("TIMEOUT",ti)
	assert(session)
	local succ, ret = coroutine_yield("SLEEP", session)
	sleep_session[coroutine.running()] = nil
	if succ then
		return
	end
	if ret == "BREAK" then
		return "BREAK"
	else
		error(ret)
	end
end
--切出
function ztask.yield(session)
	if session then
		watching_session[session] = service
		local succ, msg, sz = coroutine_yield("CALL", session)
		watching_session[session] = nil
		if not succ then
			error "call failed"
		end
		return msg,sz
	else
		return ztask.sleep(0)
	end
end
--等待
function ztask.wait(co)
	local session = c.genid()
	local ret, msg = coroutine_yield("SLEEP", session)
	co = co or coroutine.running()
	sleep_session[co] = nil
	session_id_coroutine[session] = nil
end

local self_handle
function ztask.self()
	if self_handle then
		return self_handle
	end
	self_handle = string_to_handle(c.command("REG"))
	return self_handle
end

function ztask.localname(name)
	local addr = c.command("QUERY", name)
	if addr then
		return string_to_handle(addr)
	end
end

ztask.now = c.now

local starttime

function ztask.starttime()
	if not starttime then
		starttime = c.intcommand("STARTTIME")
	end
	return starttime
end

function ztask.time()
	return ztask.now()/100 + (starttime or ztask.starttime())
end
--退出服务
function ztask.exit()
	fork_queue = {}	-- no fork coroutine can be execute after ztask.exit
	ztask.send(".launcher","lua","REMOVE",ztask.self(), false)
	-- report the sources that call me
	for co, session in pairs(session_coroutine_id) do
		local address = session_coroutine_address[co]
		if session~=0 and address then
			c.send(address, ztask.PTYPE_ERROR, session, "")
		end
	end
	for resp in pairs(unresponse) do
		resp(false)
	end
	-- report the sources I call but haven't return
	local tmp = {}
	for session, address in pairs(watching_session) do
		tmp[address] = true
	end
	for address in pairs(tmp) do
		c.send(address, ztask.PTYPE_ERROR, 0, "")
	end
	c.command("EXIT")
	-- quit service
	coroutine_yield "QUIT"
end

function ztask.getenv(key)
	return (c.command("GETENV",key))
end

function ztask.setenv(key, value)
	c.command("SETENV",key .. " " ..value)
end
--发送消息
function ztask.send(addr, typename, ...)
	local p = proto[typename]
	return c.send(addr, p.id, 0 , p.pack(...))
end

function ztask.rawsend(addr, typename, msg, sz)
	local p = proto[typename]
	return c.send(addr, p.id, 0 , msg, sz)
end

ztask.genid = assert(c.genid)
--
ztask.redirect = function(dest,source,typename,...)
	return c.redirect(dest, source, proto[typename].id, ...)
end

ztask.pack = assert(c.pack)
ztask.packstring = assert(c.packstring)
ztask.unpack = assert(c.unpack)
ztask.tostring = assert(c.tostring)
ztask.trash = assert(c.trash)

local function yield_call(service, session)
	watching_session[session] = service
	local succ, msg, sz = coroutine_yield("CALL", session)
	watching_session[session] = nil
	if not succ then
		error "call failed"
	end
	return msg,sz
end

function ztask.call(addr, typename, ...)
	local p = proto[typename]
	local session = c.send(addr, p.id , nil , p.pack(...))
	if session == nil then
		error("call to invalid address " .. ztask.address(addr))
	end
	return p.unpack(yield_call(addr, session))
end

function ztask.rawcall(addr, typename, msg, sz)
	local p = proto[typename]
	local session = assert(c.send(addr, p.id , nil , msg, sz), "call to invalid address")
	return yield_call(addr, session)
end

function ztask.ret(msg, sz)
	msg = msg or ""
	return coroutine_yield("RETURN", msg, sz)
end

function ztask.response(pack)
	pack = pack or ztask.pack
	return coroutine_yield("RESPONSE", pack)
end

function ztask.retpack(...)
	return ztask.ret(ztask.pack(...))
end
--唤醒协程
function ztask.wakeup(co)
	if sleep_session[co] then
		table.insert(wakeup_queue, co)
		return true
	end
end

function ztask.dispatch(typename, func)
	local p = proto[typename]
	if func then
		local ret = p.dispatch
		p.dispatch = func
		return ret
	else
		return p and p.dispatch
	end
end

local function unknown_request(session, address, msg, sz, prototype)
	ztask.error(string.format("Unknown request (%s): %s", prototype, c.tostring(msg,sz)))
	error(string.format("Unknown session : %d from %x", session, address))
end

function ztask.dispatch_unknown_request(unknown)
	local prev = unknown_request
	unknown_request = unknown
	return prev
end

local function unknown_response(session, address, msg, sz)
	ztask.error(string.format("Response message : %s" , c.tostring(msg,sz)))
	error(string.format("Unknown session : %d from %x", session, address))
end

function ztask.dispatch_unknown_response(unknown)
	local prev = unknown_response
	unknown_response = unknown
	return prev
end
--创建协程
function ztask.fork(func,...)
	local args = table.pack(...)
	local co = co_create(function()
		func(table.unpack(args,1,args.n))
	end)
	table.insert(fork_queue, co)
	return co
end

local function raw_dispatch_message(prototype, msg, sz, session, source)
	-- ztask.PTYPE_RESPONSE = 1, read ztask.h
	if prototype == 1 then
		local co = session_id_coroutine[session]
		if co == "BREAK" then
			session_id_coroutine[session] = nil
		elseif co == nil then
			unknown_response(session, source, msg, sz)
		else
			session_id_coroutine[session] = nil
			suspend(co, coroutine_resume(co, true, msg, sz))
		end
	else
		local p = proto[prototype]
		if p == nil then
			if session ~= 0 then
				c.send(source, ztask.PTYPE_ERROR, session, "")
			else
				unknown_request(session, source, msg, sz, prototype)
			end
			return
		end
		local f = p.dispatch
		if f then
			local ref = watching_service[source]
			if ref then
				watching_service[source] = ref + 1
			else
				watching_service[source] = 1
			end
			local co = co_create(f)
			session_coroutine_id[co] = session
			session_coroutine_address[co] = source
			suspend(co, coroutine_resume(co, session,source, p.unpack(msg,sz)))
		elseif session ~= 0 then
			c.send(source, ztask.PTYPE_ERROR, session, "")
		else
			unknown_request(session, source, msg, sz, proto[prototype].name)
		end
	end
end

function ztask.dispatch_message(...)
	local succ, err = pcall(raw_dispatch_message,...)
	while true do
		local key,co = next(fork_queue)
		if co == nil then
			break
		end
		fork_queue[key] = nil
		local fork_succ, fork_err = pcall(suspend,co,coroutine_resume(co))
		if not fork_succ then
			if succ then
				succ = false
				err = tostring(fork_err)
			else
				err = tostring(err) .. "\n" .. tostring(fork_err)
			end
		end
	end
	assert(succ, tostring(err))
end
--新建服务
function ztask.newservice(name, ...)
	return ztask.call(".launcher", "lua" , "LAUNCH", "snlua", name, ...)
end
--新建服务(具有唯一性)
function ztask.uniqueservice(global, ...)
	if global == true then
		return assert(ztask.call(".service", "lua", "GLAUNCH", ...))
	else
		return assert(ztask.call(".service", "lua", "LAUNCH", global, ...))
	end
end

function ztask.queryservice(global, ...)
	if global == true then
		return assert(ztask.call(".service", "lua", "GQUERY", ...))
	else
		return assert(ztask.call(".service", "lua", "QUERY", global, ...))
	end
end
--转换地址
function ztask.address(addr)
	if type(addr) == "number" then
		return string.format(":%08x",addr)
	else
		return tostring(addr)
	end
end

function ztask.harbor(addr)
	return c.harbor(addr)
end

ztask.error = c.error

----- 注册协议
do
	local REG = ztask.register_protocol

	REG {
		name = "lua",
		id = ztask.PTYPE_LUA,
		pack = ztask.pack,
		unpack = ztask.unpack,
	}

	REG {
		name = "response",
		id = ztask.PTYPE_RESPONSE,
	}

	REG {
		name = "error",
		id = ztask.PTYPE_ERROR,
		unpack = function(...) return ... end,
		dispatch = _error_dispatch,
	}
end

local init_func = {}

function ztask.init(f, name)
	assert(type(f) == "function")
	if init_func == nil then
		f()
	else
		table.insert(init_func, f)
		if name then
			assert(type(name) == "string")
			assert(init_func[name] == nil)
			init_func[name] = f
		end
	end
end

local function init_all()
	local funcs = init_func
	init_func = nil
	if funcs then
		for _,f in ipairs(funcs) do
			f()
		end
	end
end

local function ret(f, ...)
	f()
	return ...
end

local function init_template(start, ...)
	init_all()
	init_func = {}
	return ret(init_all, start(...))
end

function ztask.pcall(start, ...)
	return xpcall(init_template, debug.traceback, start, ...)
end

function ztask.init_service(start)
	local ok, err = ztask.pcall(start)
	if not ok then
		ztask.error("init service failed: " .. tostring(err))
		ztask.send(".launcher","lua", "ERROR")
		ztask.exit()
	else
		ztask.send(".launcher","lua", "LAUNCHOK")
	end
end

function ztask.start(start_func)
	c.callback(ztask.dispatch_message)
	ztask.timeout(0, function()
		ztask.init_service(start_func)
	end)
end

function ztask.endless()
	return (c.intcommand("STAT", "endless") == 1)
end
--队列长度
function ztask.mqlen()
	return c.intcommand("STAT", "mqlen")
end

function ztask.stat(what)
	return c.intcommand("STAT", what)
end

function ztask.task(ret)
	local t = 0
	for session,co in pairs(session_id_coroutine) do
		if ret then
			ret[session] = debug.traceback(co)
		end
		t = t + 1
	end
	return t
end

function ztask.term(service)
	return _error_dispatch(0, service)
end
--
function ztask.memlimit(bytes)
	debug.getregistry().memlimit = bytes
	ztask.memlimit = nil	-- set only once
end

-- Inject internal debug framework
ztask.debug = require "ztask.debug"
ztask.debug.init(ztask, {
	dispatch = ztask.dispatch_message,
	suspend = suspend,
})

return ztask
