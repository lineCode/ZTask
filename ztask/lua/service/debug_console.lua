local ztask = require "ztask"
require "ztask.manager"
local codecache = require "ztask.codecache"
local core = require "ztask.core"
local socket = require "ztask.socket"
local memory = require "ztask.memory"
local sockethelper = require "http.sockethelper"

local arg = table.pack(...)
assert(arg.n <= 2)
local ip = (arg.n == 2 and arg[1] or "127.0.0.1")
local port = tonumber(arg[arg.n])

local COMMAND = {}
local COMMANDX = {}

local function format_table(t)
	local index = {}
	for k in pairs(t) do
		table.insert(index, k)
	end
	table.sort(index, function(a, b) return tostring(a) < tostring(b) end)
	local result = {}
	for _,v in ipairs(index) do
		table.insert(result, string.format("%s:%s",v,tostring(t[v])))
	end
	return table.concat(result,"\t")
end

local function dump_line(print, key, value)
	if type(value) == "table" then
		print(key, format_table(value))
	else
		print(key,tostring(value))
	end
end

local function dump_list(print, list)
	local index = {}
	for k in pairs(list) do
		table.insert(index, k)
	end
	table.sort(index, function(a, b) return tostring(a) < tostring(b) end)
	for _,v in ipairs(index) do
		dump_line(print, v, list[v])
	end
end

local function split_cmdline(cmdline)
	local split = {}
	for i in string.gmatch(cmdline, "%S+") do
		table.insert(split,i)
	end
	return split
end

local function docmd(cmdline, print, fd)
	local split = split_cmdline(cmdline)
	local command = split[1]
	local cmd = COMMAND[command]
	local ok, list
	print("<CMD " .. command .. ">")
	if cmd then
		ok, list = pcall(cmd, table.unpack(split,2))
	else
		cmd = COMMANDX[command]
		if cmd then
			split.fd = fd
			split[1] = cmdline
			ok, list = pcall(cmd, split)
		else
			print("Invalid command, type help for command list")
		end
	end

	if ok then
		if list then
			if type(list) == "string" then
				print(list)
			else
				dump_list(print, list)
			end
		end
		print("<CMD OK>")
	else
		print(list)
		print("<CMD Error>")
	end
end

local function console_main_loop(stdin, print)
	print("<CMD hello>\r\nWelcome to ztask console\r\n</CMD OK>")
	ztask.error(stdin, "connected")
	local ok, err = pcall(function()
		while true do
			local cmdline = socket.readline(stdin, "\n")
			if not cmdline then
				break
			end
			if cmdline ~= "" then
				docmd(cmdline, print, stdin)
			end
		end
	end)
	if not ok then
		ztask.error(stdin, err)
	end
	ztask.error(stdin, "disconnected")
	socket.close(stdin)
end
--注册本地协议
ztask.dispatch("lua", function(session, address, cmd , ...)
	cmd = string.upper(cmd)
	local f = command[cmd]
	if f then
		local ret = f(address, ...)
		if ret ~= NORET then
			ztask.ret(ztask.pack(ret))
		end
	else
		ztask.ret(ztask.pack {"Unknown command"} )
	end
end)
--调试器启动
ztask.start(function()
	ztask.alias("系统调试服务")
	--监听端口
	local listen_socket = socket.listen (ip, port)
	ztask.error("启动调试控制台 " .. ip .. ":" .. port)
	--接受客户连接
	socket.accept(listen_socket , function(id, addr)
		local function print(...)
			local t = { ... }
			for k,v in ipairs(t) do
				t[k] = tostring(v)
			end
			socket.write(id, table.concat(t,"\t"))
			socket.write(id, "\r\n")
		end
		socket.start(id)
		ztask.fork(console_main_loop, id , print)
	end)

end)

function COMMAND.help()
	return {
		help = "This help message",
		list = "List all the service",
		stat = "Dump all stats",
		info = "info address : get service infomation",
		exit = "exit address : kill a lua service",
		kill = "kill address : kill service",
		mem = "mem : show memory status",
		gc = "gc : force every lua service do garbage collect",
		start = "lanuch a new lua service",
		clearcache = "clear lua code cache",
		service = "List unique service",
		task = "task address : show service task detail",
		inject = "inject address luascript.lua",
		logon = "logon address",
		logoff = "logoff address",
		log = "launch a new lua service with log",
		debug = "debug address : debug a lua service",
		signal = "signal address sig",
		cmem = "Show C memory info",
		shrtbl = "Show shared short string table info",
		ping = "ping address",
		call = "call address ...",
	}
end

function COMMAND.clearcache()
	codecache.clear()
end
--启动一个服务
function COMMAND.start(...)
	local ok, addr = pcall(ztask.newservice, ...)
	if ok then
		if addr then
			return { [ztask.address(addr)] = ... }
		else
			return "Exit"
		end
	else
		return "Failed"
	end
end

function COMMAND.log(...)
	local ok, addr = pcall(ztask.call, ".launcher", "lua", "LOGLAUNCH", "snlua", ...)
	if ok then
		if addr then
			return { [ztask.address(addr)] = ... }
		else
			return "Failed"
		end
	else
		return "Failed"
	end
end

function COMMAND.service()
	return ztask.call("SERVICE", "lua", "LIST")
end

local function adjust_address(address)
	if address:sub(1,1) ~= ":" then
		address = assert(tonumber("0x" .. address), "Need an address") | (ztask.harbor(ztask.self()) << 24)
	end
	return address
end

function COMMAND.list()
	return ztask.call(".launcher", "lua", "LIST")
end

function COMMAND.stat()
	return ztask.call(".launcher", "lua", "STAT")
end

function COMMAND.mem()
	return ztask.call(".launcher", "lua", "MEM")
end
--杀死一个服务
function COMMAND.kill(address)
	return ztask.call(".launcher", "lua", "KILL", address)
end

function COMMAND.gc()
	return ztask.call(".launcher", "lua", "GC")
end

function COMMAND.exit(address)
	ztask.send(adjust_address(address), "debug", "EXIT")
end

function COMMAND.inject(address, filename)
	address = adjust_address(address)
	local f = io.open(filename, "rb")
	if not f then
		return "Can't open " .. filename
	end
	local source = f:read "*a"
	f:close()
	local ok, output = ztask.call(address, "debug", "RUN", source, filename)
	if ok == false then
		error(output)
	end
	return output
end

function COMMAND.task(address)
	address = adjust_address(address)
	return ztask.call(address,"debug","TASK")
end

function COMMAND.info(address, ...)
	address = adjust_address(address)
	return ztask.call(address,"debug","INFO", ...)
end

function COMMANDX.debug(cmd)
	local address = adjust_address(cmd[2])
	local agent = ztask.newservice "debug_agent"
	local stop
	local term_co = coroutine.running()
	local function forward_cmd()
		repeat
			-- notice :  It's a bad practice to call socket.readline from two threads (this one and console_main_loop), be careful.
			ztask.call(agent, "lua", "ping")	-- detect agent alive, if agent exit, raise error
			local cmdline = socket.readline(cmd.fd, "\n")
			cmdline = cmdline and cmdline:gsub("(.*)\r$", "%1")
			if not cmdline then
				ztask.send(agent, "lua", "cmd", "cont")
				break
			end
			ztask.send(agent, "lua", "cmd", cmdline)
		until stop or cmdline == "cont"
	end
	ztask.fork(function()
		pcall(forward_cmd)
		if not stop then	-- block at ztask.call "start"
			term_co = nil
		else
			ztask.wakeup(term_co)
		end
	end)
	local ok, err = ztask.call(agent, "lua", "start", address, cmd.fd)
	stop = true
	if term_co then
		-- wait for fork coroutine exit.
		ztask.wait(term_co)
	end

	if not ok then
		error(err)
	end
end

function COMMAND.logon(address)
	address = adjust_address(address)
	core.command("LOGON", ztask.address(address))
end

function COMMAND.logoff(address)
	address = adjust_address(address)
	core.command("LOGOFF", ztask.address(address))
end

function COMMAND.signal(address, sig)
	address = ztask.address(adjust_address(address))
	if sig then
		core.command("SIGNAL", string.format("%s %d",address,sig))
	else
		core.command("SIGNAL", address)
	end
end

function COMMAND.cmem()
	local info = memory.info()
	local tmp = {}
	for k,v in pairs(info) do
		tmp[ztask.address(k)] = v
	end
	tmp.total = memory.total()
	tmp.block = memory.block()

	return tmp
end

function COMMAND.shrtbl()
	local n, total, longest, space = memory.ssinfo()
	return { n = n, total = total, longest = longest, space = space }
end

function COMMAND.ping(address)
	address = adjust_address(address)
	local ti = ztask.now()
	ztask.call(address, "debug", "PING")
	ti = ztask.now() - ti
	return tostring(ti)
end

function COMMANDX.call(cmd)
	local address = adjust_address(cmd[2])
	local cmdline = assert(cmd[1]:match("%S+%s+%S+%s(.+)") , "need arguments")
	local args_func = assert(load("return " .. cmdline, "debug console", "t", {}), "Invalid arguments")
	local args = table.pack(pcall(args_func))
	if not args[1] then
		error(args[2])
	end
	local rets = table.pack(ztask.call(address, "lua", table.unpack(args, 2, args.n)))
	return rets
end
