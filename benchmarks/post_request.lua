-- wrk Lua script for POST requests with JSON body
-- Usage: wrk -s post_request.lua http://localhost:8080/api/hello

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"
wrk.body = '{"message":"benchmark test","data":"some payload"}'

-- Optional: Track latency distribution
done = function(summary, latency, requests)
    io.write("------------------------------\n")
    io.write(string.format("Requests:      %d\n", summary.requests))
    io.write(string.format("Duration:      %.2fs\n", summary.duration / 1000000))
    io.write(string.format("Req/sec:       %.2f\n", summary.requests / (summary.duration / 1000000)))
    io.write(string.format("Latency avg:   %.2fms\n", latency.mean / 1000))
    io.write(string.format("Latency stdev: %.2fms\n", latency.stdev / 1000))
    io.write(string.format("Latency min:   %.2fms\n", latency.min / 1000))
    io.write(string.format("Latency max:   %.2fms\n", latency.max / 1000))
    io.write("------------------------------\n")
end

