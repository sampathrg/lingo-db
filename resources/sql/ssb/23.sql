select d_year, p_brand, sum(cast(lo_revenue as int8))
from lineorder, date, part, supplier
where lo_orderdate = d_datekey
and lo_partkey = p_partkey
and lo_suppkey = s_suppkey
and p_brand= 'MFGR#2221'
and s_region = 'EUROPE'
group by d_year, p_brand
