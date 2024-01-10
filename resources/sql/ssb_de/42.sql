select d_year, s_nation, p_category, sum(cast(lo_revenue as int8) - cast(lo_supplycost as int8)) as profit
from date_dict_encoded_str, customer_SSB_dict_encoded_str, supplier_dict_encoded_str, part_dict_encoded_str, lineorder
where lo_custkey = c_custkey
and lo_suppkey = s_suppkey
and lo_partkey = p_partkey
and lo_orderdate = d_datekey
and c_region = 2 -- c_region = 'AMERICA'
and s_region = 0 -- s_region = 'AMERICA'
and (d_year = 1997 or d_year = 1998)
and (p_mfgr = 0 or p_mfgr = 3) -- (p_mfgr = 'MFGR#1' or p_mfgr = 'MFGR#2') TODO - Check if this is correct.
group by d_year, s_nation, p_category
-- order by d_year, s_nation, p_category

-- select d_year, s_nation, p_category,
-- sum(lo_revenue - lo_supplycost) as profit
-- from date, customer, supplier, part, lineorder
-- where lo_custkey = c_custkey
-- and lo_suppkey = s_suppkey
-- and lo_partkey = p_partkey
-- and lo_orderdate = d_datekey
-- and c_region = 'AMERICA'
-- and s_region = 'AMERICA'
-- and (d_year = 1997 or d_year = 1998)
-- and (p_mfgr = 'MFGR#1'
--    or p_mfgr = 'MFGR#2')
-- group by d_year, s_nation, p_category
-- order by d_year, s_nation, p_category;
