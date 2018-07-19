local table = table
local extern_dbgcmd = {}

local function init(ztask, export)
	local internal_info_func

	function ztask.info_func(func)
		internal_info_func = func
	end

	local dbgcmd

	local function init_dbgcmd()
		dbgcmd = {}

		function dbgcmd.MEM()
			local kb, bytes = collectgarbage "count"
			ztask.ret(ztask.pack(kb,bytes))
		end

		function dbgcmd.GC()

			collectgarbage "collect"
		end

		function dbgcmd.STAT()
			local stat = {}
			stat.task = ztask.task()
			stat.mqlen = ztask.stat "mqlen"
			stat.cpu = ztask.stat "cpu"
			stat.message = ztask.stat "message"
			ztask.ret(ztask.pack(stat))
		end

		function dbgcmd.TASK()
			local task = {}
			ztask.task(task)
			ztask.ret(ztask.pack(task))
		end

		function dbgcmd.INFO(...)
			if internal_info_func then
				ztask.ret(ztask.pack(internal_info_func(...)))
			else
				ztask.ret(ztask.pack(nil))
			end
		end

		function dbgcmd.EXIT()
			ztask.exit()
		end

		function dbgcmd.RUN(source, filename)
			local inject = require "ztask.inject"
			local ok, output = inject(ztask, source, filename , export.dispatch, ztask.register_protocol)
			collectgarbage "collect"
			ztask.ret(ztask.pack(ok, table.concat(output, "\n")))
		end

		function dbgcmd.TERM(service)
			ztask.term(service)
		end

		function dbgcmd.REMOTEDEBUG(...)
			local remotedebug = require "ztask.remotedebug"
			remotedebug.start(export, ...)
		end

		function dbgcmd.SUPPORT(pname)
			return ztask.ret(ztask.pack(ztask.dispatch(pname) ~= nil))
		end

		function dbgcmd.PING()
			return ztask.ret()
		end

		function dbgcmd.LINK()
			ztask.response()	-- get response , but not return. raise error when exit
		end

		return dbgcmd
	end -- function init_dbgcmd
	--调度调试请求
	local function _debug_dispatch(session, address, cmd, ...)
		dbgcmd = dbgcmd or init_dbgcmd() -- lazy init dbgcmd
		local f = dbgcmd[cmd] or extern_dbgcmd[cmd]
		assert(f, cmd)
		f(...)
	end
	--注册调试协议
	ztask.register_protocol {
		name = "debug",
		id = assert(ztask.PTYPE_DEBUG),
		pack = assert(ztask.pack),
		unpack = assert(ztask.unpack),
		dispatch = _debug_dispatch,
	}
end

local function reg_debugcmd(name, fn)
	extern_dbgcmd[name] = fn
end

return {
	init = init,
	reg_debugcmd = reg_debugcmd,
}
