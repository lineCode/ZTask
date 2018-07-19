local ztask = require "ztask"
require "ztask.manager"	-- import ztask.launch, ...

local globalname = {}
local queryname = {}
local harbor = {}
local harbor_service

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

function harbor.REGISTER(name, handle)
	assert(globalname[name] == nil)
	globalname[name] = handle
	response_name(name)
	ztask.redirect(harbor_service, handle, "harbor", 0, "N " .. name)
end

function harbor.QUERYNAME(name)
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
		queue = { ztask.response() }
		queryname[name] = queue
	else
		table.insert(queue, ztask.response())
	end
end

function harbor.LINK(id)
	ztask.ret()
end

function harbor.CONNECT(id)
	ztask.error("Can't connect to other harbor in single node mode")
end

ztask.start(function()
	ztask.alias("cdummy")
	local harbor_id = tonumber(ztask.getenv "harbor")
	assert(harbor_id == 0)

	ztask.dispatch("lua", function (session,source,command,...)
		local f = assert(harbor[command])
		f(...)
	end)
	ztask.dispatch("text", function(session,source,command)
		-- ignore all the command
	end)

	harbor_service = assert(ztask.launch("harbor", harbor_id, ztask.self()))
end)
