select d_year, s_nation, c_nation, sum(cast(lo_revenue as int8)) as revenue
from customer_dict_encoded_str, lineorder, supplier_dict_encoded_str, date_dict_encoded_str
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_orderdate = d_datekey
and c_region = 3 -- c_region = 'ASIA'
and s_region = 4 -- s_region = 'ASIA'
and d_year >= 1992 and d_year <= 1997
group by c_nation, s_nation, d_year
-- order by d_year asc, revenue desc; -- How are we doing this?
