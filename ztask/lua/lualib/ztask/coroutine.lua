-- You should use this module (ztask.coroutine) instead of origin lua coroutine in ztask framework

local coroutine = coroutine
-- origin lua coroutine module
local coroutine_resume = coroutine.resume
local coroutine_yield = coroutine.yield
local coroutine_status = coroutine.status
local coroutine_running = coroutine.running

local select = select
local ztaskco = {}

ztaskco.isyieldable = coroutine.isyieldable
ztaskco.running = coroutine.running
ztaskco.status = coroutine.status

local ztask_coroutines = setmetatable({}, { __mode = "kv" })

function ztaskco.create(f)
	local co = coroutine.create(f)
	-- mark co as a ztask coroutine
	ztask_coroutines[co] = true
	return co
end

do -- begin ztaskco.resume

	local profile = require "profile"
	-- ztask use profile.resume_co/yield_co instead of coroutine.resume/yield

	local ztask_resume = profile.resume_co
	local ztask_yield = profile.yield_co

	local function unlock(co, ...)
		ztask_coroutines[co] = true
		return ...
	end

	local function ztask_yielding(co, from, ...)
		ztask_coroutines[co] = false
		return unlock(co, ztask_resume(co, from, ztask_yield(from, ...)))
	end

	local function resume(co, from, ok, ...)
		if not ok then
			return ok, ...
		elseif coroutine_status(co) == "dead" then
			-- the main function exit
			ztask_coroutines[co] = nil
			return true, ...
		elseif (...) == "USER" then
			return true, select(2, ...)
		else
			-- blocked in ztask framework, so raise the yielding message
			return resume(co, from, ztask_yielding(co, from, ...))
		end
	end

	-- record the root of coroutine caller (It should be a ztask thread)
	local coroutine_caller = setmetatable({} , { __mode = "kv" })

function ztaskco.resume(co, ...)
	local co_status = ztask_coroutines[co]
	if not co_status then
		if co_status == false then
			-- is running
			return false, "cannot resume a ztask coroutine suspend by ztask framework"
		end
		if coroutine_status(co) == "dead" then
			-- always return false, "cannot resume dead coroutine"
			return coroutine_resume(co, ...)
		else
			return false, "cannot resume none ztask coroutine"
		end
	end
	local from = coroutine_running()
	local caller = coroutine_caller[from] or from
	coroutine_caller[co] = caller
	return resume(co, caller, coroutine_resume(co, ...))
end

function ztaskco.thread(co)
	co = co or coroutine_running()
	if ztask_coroutines[co] ~= nil then
		return coroutine_caller[co] , false
	else
		return co, true
	end
end

end -- end of ztaskco.resume

function ztaskco.status(co)
	local status = coroutine.status(co)
	if status == "suspended" then
		if ztask_coroutines[co] == false then
			return "blocked"
		else
			return "suspended"
		end
	else
		return status
	end
end

function ztaskco.yield(...)
	return coroutine_yield("USER", ...)
end

do -- begin ztaskco.wrap

	local function wrap_co(ok, ...)
		if ok then
			return ...
		else
			error(...)
		end
	end

function ztaskco.wrap(f)
	local co = ztaskco.create(function(...)
		return f(...)
	end)
	return function(...)
		return wrap_co(ztaskco.resume(co, ...))
	end
end

end	-- end of ztaskco.wrap

return ztaskco
