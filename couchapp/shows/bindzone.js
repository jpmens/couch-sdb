// By Thomas Kerpe, based on JPMENS' view code
function(doc, req) {
    // bind zone file generation as an example (CouchDB show function)
    // see http://blog.fupps.com/2010/05/03/dns-backed-by-couchdb-redux/
    // reused some code by the original author Jan-Piet Mens
    // Warning very raw code below. It may eat your pants.

    var soa_rr = function (mname, rname, serial, refresh, retry, expire, minimum){
        return ("@ IN SOA " + mname + ". " + rname + ". ("+ serial+" "+ refresh + " "+ retry + " "+ expire +" "+ minimum +")\n\n");
    };
    
    var zone = "";
    
    var zonettl = doc.default_ttl ? doc.default_ttl : 86400;
    var soa = doc.soa;
    var mname   = (soa.mname)   ? soa.mname   : 'dns.' + doc.zone;
    var rname   = (soa.rname)   ? soa.rname   : 'hostmaster.' + doc.zone;
    var serial  = (soa.serial)  ? soa.serial  : doc['_rev'].replace(/-.*/, "");
    var refresh = (soa.refresh) ? soa.refresh : 86400;
    var retry   = (soa.retry)   ? soa.retry   : 7200;
    var expire  = (soa.expire)  ? soa.expire  : 3600000;
    var minimum = (soa.minimum) ? soa.minimum : 172800;
    
    zone += "$TTL " + zonettl + "\n";
    
    zone += soa_rr(mname, rname, serial, refresh, retry, expire, minimum);
    
    if (doc.ns && doc.ns.length > 0) {
        doc.ns.forEach(function(addr) {
            zone += "\tIN NS " + addr + ".\n";
        });
    }
    zone += "\n";
    
    if (doc.rr && doc.rr.length > 0) {
        for (var i = 0; i < doc.rr.length; i++) {
            var rr = doc.rr[i];
            var rr_type = rr.type.toUpperCase();
            var ttl = (rr.ttl) ? rr.ttl : zonettl;
            var fqdn = (rr.name) ? rr.name + '.' : '';
            
            if (rr_type === "MX" && rr.priority){
                var rr_data = rr.priority + " " + rr.data;
            } else {
                var rr_data = rr.data;
                if (rr_type === "TXT"){
                    // Quote here
                    rr_data = "\""+rr_data+"\"";
                }
            }

            fqdn += doc.zone;
            zone += fqdn +".\t"+ ttl +" IN " + rr_type + " " + rr_data + "\n";
        }
    }
    
    return (zone);
}
