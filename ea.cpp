//+------------------------------------------------------------------+
//| StopReverse_Trailing_SL.mq5                                      |
//| Stop-Reverse EA with Dynamic SL following opposite pending      |
//|                                                                  |
//| Core idea:                                                       |
//| 1 active position + 1 opposite pending order                    |
//|                                                                  |
//| SELL position -> BUY STOP + SL SELL follows BUY STOP             |
//| BUY position  -> SELL STOP + SL BUY follows SELL STOP            |
//|                                                                  |
//| Pending order can trail after profit threshold.                  |
//| Position SL follows the pending order automatically.             |
//+------------------------------------------------------------------+
#property strict
#property version "2.00"

#include <Trade/Trade.mqh>
CTrade trade;

//--------------------------- INPUTS ---------------------------------

input ulong  MagicNumber          = 16042026;
input double Lots                 = 0.3;

// Initial direction
// false = SELL
// true  = BUY
input bool   StartWithBuy         = false;

// Initial distance for opposite pending order.
// Example XAUUSD:
// SELL @ 4000
// BUY STOP @ 4003.36
input double ReverseDistance      = 3.36;

// When floating profit reaches this account-currency amount,
// the opposite pending order starts trailing.
//
// 0 = trailing from the beginning.
input double TrailStartProfit     = 20.0;

// Distance between market price and trailing pending order.
input double TrailDistance        = 0.30;

// Minimum movement before modifying the pending order.
input double TrailStep            = 0.05;

// Optional fixed TP.
// 0 = disabled.
//
// NOTE:
// SL is NOT controlled by this parameter anymore.
// SL automatically follows the opposite pending order.
input double TakeProfitDistance   = 0.0;

input int    DeviationPoints      = 50;

//--------------------------- HELPERS --------------------------------

string Sym()
{
   return _Symbol;
}

// Normalize price according to symbol digits.
double NormalizePrice(double price)
{
   return NormalizeDouble(
      price,
      (int)SymbolInfoInteger(Sym(), SYMBOL_DIGITS)
   );
}

// Check whether a position belongs to this EA.
bool IsOurPosition(ulong ticket)
{
   if(!PositionSelectByTicket(ticket))
      return false;

   return
      PositionGetString(POSITION_SYMBOL) == Sym() &&
      (ulong)PositionGetInteger(POSITION_MAGIC) == MagicNumber;
}

// Check whether an order belongs to this EA.
bool IsOurOrder(ulong ticket)
{
   if(!OrderSelect(ticket))
      return false;

   return
      OrderGetString(ORDER_SYMBOL) == Sym() &&
      (ulong)OrderGetInteger(ORDER_MAGIC) == MagicNumber;
}

// Count our active positions.
int OurPositionCount()
{
   int n = 0;

   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong t = PositionGetTicket(i);

      if(t > 0 && IsOurPosition(t))
         n++;
   }

   return n;
}

// Get our active position.
bool GetOurPosition(
   ulong &ticket,
   ENUM_POSITION_TYPE &type,
   double &openPrice,
   double &profit
)
{
   for(int i = PositionsTotal() - 1; i >= 0; i--)
   {
      ulong t = PositionGetTicket(i);

      if(t == 0 || !IsOurPosition(t))
         continue;

      ticket    = t;
      type      = (ENUM_POSITION_TYPE)
                  PositionGetInteger(POSITION_TYPE);

      openPrice = PositionGetDouble(POSITION_PRICE_OPEN);
      profit    = PositionGetDouble(POSITION_PROFIT);

      return true;
   }

   return false;
}

// Get our BUY STOP / SELL STOP.
bool GetOurPending(
   ulong &ticket,
   ENUM_ORDER_TYPE &type,
   double &price
)
{
   for(int i = OrdersTotal() - 1; i >= 0; i--)
   {
      ulong t = OrderGetTicket(i);

      if(t == 0 || !IsOurOrder(t))
         continue;

      ENUM_ORDER_TYPE ot =
         (ENUM_ORDER_TYPE)OrderGetInteger(ORDER_TYPE);

      if(ot != ORDER_TYPE_BUY_STOP &&
         ot != ORDER_TYPE_SELL_STOP)
         continue;

      ticket = t;
      type   = ot;
      price  = OrderGetDouble(ORDER_PRICE_OPEN);

      return true;
   }

   return false;
}

// Delete all BUY STOP / SELL STOP belonging to this EA.
void DeleteOurPendings()
{
   for(int i = OrdersTotal() - 1; i >= 0; i--)
   {
      ulong t = OrderGetTicket(i);

      if(t == 0 || !IsOurOrder(t))
         continue;

      ENUM_ORDER_TYPE ot =
         (ENUM_ORDER_TYPE)OrderGetInteger(ORDER_TYPE);

      if(ot == ORDER_TYPE_BUY_STOP ||
         ot == ORDER_TYPE_SELL_STOP)
      {
         if(!trade.OrderDelete(t))
         {
            Print(
               "Failed deleting pending #",
               t,
               " | Retcode=",
               trade.ResultRetcode(),
               " | ",
               trade.ResultRetcodeDescription()
            );
         }
      }
   }
}

//--------------------------- OPEN ORDERS -----------------------------

bool PlaceBuyStop(double price)
{
   trade.SetExpertMagicNumber(MagicNumber);
   trade.SetDeviationInPoints(DeviationPoints);

   price = NormalizePrice(price);

   bool result = trade.BuyStop(
      Lots,
      price,
      Sym(),
      0.0,
      0.0,
      ORDER_TIME_GTC,
      0,
      "SR_BUY_STOP"
   );

   if(!result)
   {
      Print(
         "BUY STOP failed @ ",
         price,
         " | Retcode=",
         trade.ResultRetcode(),
         " | ",
         trade.ResultRetcodeDescription()
      );
   }

   return result;
}

bool PlaceSellStop(double price)
{
   trade.SetExpertMagicNumber(MagicNumber);
   trade.SetDeviationInPoints(DeviationPoints);

   price = NormalizePrice(price);

   bool result = trade.SellStop(
      Lots,
      price,
      Sym(),
      0.0,
      0.0,
      ORDER_TIME_GTC,
      0,
      "SR_SELL_STOP"
   );

   if(!result)
   {
      Print(
         "SELL STOP failed @ ",
         price,
         " | Retcode=",
         trade.ResultRetcode(),
         " | ",
         trade.ResultRetcodeDescription()
      );
   }

   return result;
}

//--------------------------- INITIAL POSITION -----------------------

bool OpenInitialPosition()
{
   trade.SetExpertMagicNumber(MagicNumber);
   trade.SetDeviationInPoints(DeviationPoints);

   if(StartWithBuy)
   {
      bool result = trade.Buy(
         Lots,
         Sym(),
         0,
         0,
         0,
         "SR_INITIAL_BUY"
      );

      if(!result)
      {
         Print(
            "Initial BUY failed | Retcode=",
            trade.ResultRetcode(),
            " | ",
            trade.ResultRetcodeDescription()
         );
      }

      return result;
   }

   bool result = trade.Sell(
      Lots,
      Sym(),
      0,
      0,
      0,
      "SR_INITIAL_SELL"
   );

   if(!result)
   {
      Print(
         "Initial SELL failed | Retcode=",
         trade.ResultRetcode(),
         " | ",
         trade.ResultRetcodeDescription()
      );
   }

   return result;
}

//--------------------------- TAKE PROFIT ----------------------------

void ApplyTakeProfit()
{
   if(TakeProfitDistance <= 0)
      return;

   ulong ticket;
   ENUM_POSITION_TYPE type;
   double openPrice;
   double profit;

   if(!GetOurPosition(
         ticket,
         type,
         openPrice,
         profit
      ))
   {
      return;
   }

   double tp = 0.0;

   if(type == POSITION_TYPE_BUY)
   {
      tp = NormalizePrice(
         openPrice + TakeProfitDistance
      );
   }
   else
   {
      tp = NormalizePrice(
         openPrice - TakeProfitDistance
      );
   }

   // Get current SL so that setting TP does not remove dynamic SL.
   if(!PositionSelectByTicket(ticket))
      return;

   double currentSL =
      PositionGetDouble(POSITION_SL);

   if(!trade.PositionModify(
         Sym(),
         currentSL,
         tp
      ))
   {
      Print(
         "TP modification failed | Retcode=",
         trade.ResultRetcode(),
         " | ",
         trade.ResultRetcodeDescription()
      );
   }
}

//--------------------------- DYNAMIC SL -----------------------------

// Calculate the SL that should follow the opposite pending order.
//
// SELL position:
//    BUY STOP price becomes SELL SL.
//
// BUY position:
//    SELL STOP price becomes BUY SL.
//
// Important:
// The SL must satisfy broker's minimum stop distance.

double CalculateSLFromPending(
   ENUM_POSITION_TYPE positionType,
   double pendingPrice
)
{
   double bid = SymbolInfoDouble(
      Sym(),
      SYMBOL_BID
   );

   double ask = SymbolInfoDouble(
      Sym(),
      SYMBOL_ASK
   );

   double point = SymbolInfoDouble(
      Sym(),
      SYMBOL_POINT
   );

   int stopsLevel = (int)
      SymbolInfoInteger(
         Sym(),
         SYMBOL_TRADE_STOPS_LEVEL
      );

   double minDistance =
      stopsLevel * point;

   double sl = pendingPrice;

   // BUY position:
   // SL must be below Bid.
   if(positionType == POSITION_TYPE_BUY)
   {
      double maximumSL =
         bid - minDistance;

      sl = MathMin(
         pendingPrice,
         maximumSL
      );
   }

   // SELL position:
   // SL must be above Ask.
   else
   {
      double minimumSL =
         ask + minDistance;

      sl = MathMax(
         pendingPrice,
         minimumSL
      );
   }

   return NormalizePrice(sl);
}

// Apply SL exactly from opposite pending order.
void ApplyDynamicSL()
{
   ulong posTicket;
   ENUM_POSITION_TYPE posType;
   double openPrice;
   double profit;

   if(!GetOurPosition(
         posTicket,
         posType,
         openPrice,
         profit
      ))
   {
      return;
   }

   ulong pendingTicket;
   ENUM_ORDER_TYPE pendingType;
   double pendingPrice;

   // We need the reverse pending order.
   if(!GetOurPending(
         pendingTicket,
         pendingType,
         pendingPrice
      ))
   {
      return;
   }

   // Make sure pending is actually opposite.
   if(posType == POSITION_TYPE_BUY &&
      pendingType != ORDER_TYPE_SELL_STOP)
   {
      return;
   }

   if(posType == POSITION_TYPE_SELL &&
      pendingType != ORDER_TYPE_BUY_STOP)
   {
      return;
   }

   double desiredSL =
      CalculateSLFromPending(
         posType,
         pendingPrice
      );

   if(!PositionSelectByTicket(posTicket))
      return;

   double currentSL =
      PositionGetDouble(POSITION_SL);

   double currentTP =
      PositionGetDouble(POSITION_TP);

   double point =
      SymbolInfoDouble(
         Sym(),
         SYMBOL_POINT
      );

   // Avoid unnecessary PositionModify calls.
   if(currentSL > 0 &&
      MathAbs(currentSL - desiredSL) < point)
   {
      return;
   }

   if(!trade.PositionModify(
         Sym(),
         desiredSL,
         currentTP
      ))
   {
      Print(
         "Dynamic SL modification failed. ",
         "SL=",
         desiredSL,
         " | Retcode=",
         trade.ResultRetcode(),
         " | ",
         trade.ResultRetcodeDescription()
      );
   }
}

//--------------------------- REVERSE PENDING ------------------------

// Creates and maintains the opposite pending order.
//
// BUY position:
//     SELL STOP below market.
//
// SELL position:
//     BUY STOP above market.
//
// Before trailing activation:
//     pending = entry +/- ReverseDistance
//
// After trailing activation:
//     pending follows market.
//
// Dynamic SL:
//     BUY  -> SL follows SELL STOP
//     SELL -> SL follows BUY STOP

void EnsureReversePending(bool trailing)
{
   ulong posTicket;
   ENUM_POSITION_TYPE posType;
   double openPrice;
   double profit;

   if(!GetOurPosition(
         posTicket,
         posType,
         openPrice,
         profit
      ))
   {
      return;
   }

   double bid =
      SymbolInfoDouble(
         Sym(),
         SYMBOL_BID
      );

   double ask =
      SymbolInfoDouble(
         Sym(),
         SYMBOL_ASK
      );

   double point =
      SymbolInfoDouble(
         Sym(),
         SYMBOL_POINT
      );

   int stopsLevel =
      (int)SymbolInfoInteger(
         Sym(),
         SYMBOL_TRADE_STOPS_LEVEL
      );

   double minDistance =
      stopsLevel * point;

   ENUM_ORDER_TYPE wanted;
   double target;

   //==============================================================
   // BUY POSITION
   //==============================================================

   if(posType == POSITION_TYPE_BUY)
   {
      wanted = ORDER_TYPE_SELL_STOP;

      if(trailing)
      {
         target =
            bid - TrailDistance;
      }
      else
      {
         target =
            openPrice - ReverseDistance;
      }

      // SELL STOP must be sufficiently below Bid.
      target =
         MathMin(
            target,
            bid - minDistance
         );
   }

   //==============================================================
   // SELL POSITION
   //==============================================================

   else
   {
      wanted = ORDER_TYPE_BUY_STOP;

      if(trailing)
      {
         target =
            ask + TrailDistance;
      }
      else
      {
         target =
            openPrice + ReverseDistance;
      }

      // BUY STOP must be sufficiently above Ask.
      target =
         MathMax(
            target,
            ask + minDistance
         );
   }

   target = NormalizePrice(target);

   //==============================================================
   // CHECK EXISTING PENDING
   //==============================================================

   ulong orderTicket;
   ENUM_ORDER_TYPE orderType;
   double orderPrice;

   if(GetOurPending(
         orderTicket,
         orderType,
         orderPrice
      ))
   {
      // Existing pending is the wrong direction.
      if(orderType != wanted)
      {
         if(trade.OrderDelete(orderTicket))
         {
            Sleep(50);
         }

         return;
      }

      //===========================================================
      // TRAILING MODE
      //===========================================================

      if(trailing)
      {
         bool improve = false;

         // SELL STOP can only move upward?
         //
         // For a BUY position:
         // SELL STOP follows price upward.
         //
         // Therefore the new SELL STOP must be HIGHER
         // than the old SELL STOP.

         if(wanted == ORDER_TYPE_SELL_STOP &&
            target > orderPrice + TrailStep)
         {
            improve = true;
         }

         // BUY STOP follows price downward.
         //
         // Therefore the new BUY STOP must be LOWER
         // than the old BUY STOP.

         if(wanted == ORDER_TYPE_BUY_STOP &&
            target < orderPrice - TrailStep)
         {
            improve = true;
         }

         if(improve)
         {
            if(trade.OrderDelete(orderTicket))
            {
               Sleep(50);

               if(wanted == ORDER_TYPE_SELL_STOP)
               {
                  PlaceSellStop(target);
               }
               else
               {
                  PlaceBuyStop(target);
               }
            }
         }
      }

      //===========================================================
      // IMPORTANT:
      // SL follows the EXISTING pending order.
      // If pending hasn't moved, SL doesn't need to move.
      //===========================================================

      ApplyDynamicSL();

      return;
   }

   //==============================================================
   // NO PENDING ORDER EXISTS
   //==============================================================

   if(wanted == ORDER_TYPE_SELL_STOP)
   {
      PlaceSellStop(target);
   }
   else
   {
      PlaceBuyStop(target);
   }

   // SL will be synchronized on the next tick after
   // the pending order exists.
}

//--------------------------- POSITION CLEANUP -----------------------

// If multiple positions exist, keep one and close the others.
//
// NOTE:
// GetOurPosition() determines the main position based on the
// position returned by the terminal's position list.

void KeepOnlyOnePosition()
{
   ulong mainTicket;
   ENUM_POSITION_TYPE mainType;
   double mainOpen;
   double mainProfit;

   if(!GetOurPosition(
         mainTicket,
         mainType,
         mainOpen,
         mainProfit
      ))
   {
      return;
   }

   for(int i = PositionsTotal() - 1;
       i >= 0;
       i--)
   {
      ulong t =
         PositionGetTicket(i);

      if(t == 0 ||
         !IsOurPosition(t) ||
         t == mainTicket)
      {
         continue;
      }

      if(!trade.PositionClose(t))
      {
         Print(
            "Failed closing extra position #",
            t,
            " | Retcode=",
            trade.ResultRetcode(),
            " | ",
            trade.ResultRetcodeDescription()
         );
      }
   }
}

//--------------------------- PENDING CLEANUP ------------------------

void KeepOnlyOnePending()
{
   bool found = false;

   for(int i = OrdersTotal() - 1;
       i >= 0;
       i--)
   {
      ulong t =
         OrderGetTicket(i);

      if(t == 0 ||
         !IsOurOrder(t))
      {
         continue;
      }

      ENUM_ORDER_TYPE ot =
         (ENUM_ORDER_TYPE)
         OrderGetInteger(ORDER_TYPE);

      if(ot != ORDER_TYPE_BUY_STOP &&
         ot != ORDER_TYPE_SELL_STOP)
      {
         continue;
      }

      if(!found)
      {
         found = true;
      }
      else
      {
         if(!trade.OrderDelete(t))
         {
            Print(
               "Failed deleting extra pending #",
               t,
               " | Retcode=",
               trade.ResultRetcode(),
               " | ",
               trade.ResultRetcodeDescription()
            );
         }
      }
   }
}

//--------------------------- MAIN LOGIC ------------------------------

void MaintainStrategy()
{
   int posCount =
      OurPositionCount();

   //==============================================================
   // NO POSITION
   //==============================================================

   if(posCount == 0)
   {
      DeleteOurPendings();

      if(OpenInitialPosition())
      {
         Sleep(100);
      }

      return;
   }

   //==============================================================
   // KEEP ONLY ONE POSITION
   //==============================================================

   KeepOnlyOnePosition();

   //==============================================================
   // KEEP ONLY ONE PENDING
   //==============================================================

   KeepOnlyOnePending();

   //==============================================================
   // GET MAIN POSITION
   //==============================================================

   ulong mainTicket;
   ENUM_POSITION_TYPE mainType;
   double mainOpen;
   double mainProfit;

   if(!GetOurPosition(
         mainTicket,
         mainType,
         mainOpen,
         mainProfit
      ))
   {
      return;
   }

   //==============================================================
   // CHECK TRAILING ACTIVATION
   //==============================================================

   bool trailing = false;

   if(TrailStartProfit > 0 &&
      mainProfit >= TrailStartProfit)
   {
      trailing = true;
   }

   // If TrailStartProfit = 0,
   // trailing is active immediately.

   if(TrailStartProfit <= 0)
   {
      trailing = true;
   }

   //==============================================================
   // MAINTAIN OPPOSITE PENDING
   //==============================================================

   EnsureReversePending(trailing);

   //==============================================================
   // APPLY DYNAMIC SL
   //==============================================================

   ApplyDynamicSL();

   //==============================================================
   // OPTIONAL TAKE PROFIT
   //==============================================================

   ApplyTakeProfit();
}

//--------------------------- EVENTS ---------------------------------

int OnInit()
{
   trade.SetExpertMagicNumber(
      MagicNumber
   );

   trade.SetDeviationInPoints(
      DeviationPoints
   );

   Print(
      "StopReverse_Trailing_SL initialized on ",
      Sym()
   );

   Print(
      "Dynamic SL mode: ACTIVE"
   );

   return INIT_SUCCEEDED;
}

void OnTick()
{
   MaintainStrategy();
}

void OnTradeTransaction(
   const MqlTradeTransaction &trans,
   const MqlTradeRequest &request,
   const MqlTradeResult &result
)
{
   if(trans.symbol != Sym())
      return;

   // Main strategy is maintained on the next tick.
   //
   // When a pending order is triggered:
   //
   // BUY position + SELL STOP
   //        ↓
   // SELL position
   //
   // SELL position + BUY STOP
   //        ↓
   // BUY position
   //
   // MaintainStrategy() detects the new position
   // and creates the next opposite pending order.
}

//+------------------------------------------------------------------+